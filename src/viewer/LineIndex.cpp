#include "notepadFasaFiso/viewer/LineIndex.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace nff::viewer {
namespace {

[[nodiscard]] constexpr std::size_t unitWidth(const encoding::Encoding encoding) noexcept {
    switch (encoding) {
    case encoding::Encoding::Utf16LE:
    case encoding::Encoding::Utf16BE:
        return 2U;
    case encoding::Encoding::Utf32LE:
    case encoding::Encoding::Utf32BE:
        return 4U;
    default:
        return 1U;
    }
}

[[nodiscard]] std::uint32_t codeUnit(const std::byte* data,
                                     const encoding::Encoding encoding) noexcept {
    const auto b0 = std::to_integer<std::uint32_t>(data[0]);
    switch (encoding) {
    case encoding::Encoding::Utf16LE:
        return b0 | (std::to_integer<std::uint32_t>(data[1]) << 8U);
    case encoding::Encoding::Utf16BE:
        return (b0 << 8U) | std::to_integer<std::uint32_t>(data[1]);
    case encoding::Encoding::Utf32LE:
        return b0 | (std::to_integer<std::uint32_t>(data[1]) << 8U) |
               (std::to_integer<std::uint32_t>(data[2]) << 16U) |
               (std::to_integer<std::uint32_t>(data[3]) << 24U);
    case encoding::Encoding::Utf32BE:
        return (b0 << 24U) | (std::to_integer<std::uint32_t>(data[1]) << 16U) |
               (std::to_integer<std::uint32_t>(data[2]) << 8U) |
               std::to_integer<std::uint32_t>(data[3]);
    default:
        return b0;
    }
}

struct ScannerState final {
    std::uint64_t line{1U};
    std::uint64_t lineStart{};
    bool pendingCr{false};
};

}

void LineIndex::reset(const encoding::Encoding encoding,
                      const std::uint64_t contentStart,
                      const std::uint64_t fileSize,
                      LineIndexOptions options) {
    encoding_ = encoding;
    contentStart_ = std::min(contentStart, fileSize);
    fileSize_ = fileSize;
    options.checkpointStrideLines = std::max<std::uint64_t>(1U, options.checkpointStrideLines);
    options.scanChunkBytes = std::max<std::size_t>(4096U, options.scanChunkBytes);
    options_ = options;
    checkpoints_.clear();
    checkpoints_.push_back({1U, contentStart_});
    furthestIndexedOffset_ = contentStart_;
    furthestIndexedLine_ = 1U;
}

LineIndexOptions LineIndex::makeOptions(const PerformanceProfile profile) noexcept {
    if (profile == PerformanceProfile::Fast) {
        return {512U, 4U * 1024U * 1024U};
    }
    return {8192U, 1U * 1024U * 1024U};
}

LineIndex::Checkpoint LineIndex::checkpointForLine(const std::uint64_t line) const noexcept {
    const auto position = std::upper_bound(checkpoints_.begin(), checkpoints_.end(), line,
                                           [](const auto wantedLine, const Checkpoint& checkpoint) {
                                               return wantedLine < checkpoint.line;
                                           });
    return position == checkpoints_.begin() ? checkpoints_.front() : *std::prev(position);
}

LineIndex::Checkpoint LineIndex::checkpointForOffset(const std::uint64_t offset) const noexcept {
    const auto position = std::upper_bound(checkpoints_.begin(), checkpoints_.end(), offset,
                                           [](const auto wantedOffset, const Checkpoint& checkpoint) {
                                               return wantedOffset < checkpoint.offset;
                                           });
    return position == checkpoints_.begin() ? checkpoints_.front() : *std::prev(position);
}

void LineIndex::remember(const std::uint64_t line, const std::uint64_t offset) {
    if ((line - 1U) % options_.checkpointStrideLines != 0U) {
        return;
    }
    if (checkpoints_.back().line < line) {
        checkpoints_.push_back({line, offset});
        return;
    }
    const auto position = std::lower_bound(checkpoints_.begin(), checkpoints_.end(), line,
                                           [](const Checkpoint& checkpoint,
                                              const std::uint64_t wantedLine) {
                                               return checkpoint.line < wantedLine;
                                           });
    if (position != checkpoints_.end() && position->line == line) {
        return;
    }
    checkpoints_.insert(position, {line, offset});
}

LineIndex::ScanResult LineIndex::scanToLine(storage::RandomAccessFile& file,
                                            ViewCache& cache,
                                            const Checkpoint start,
                                            const std::uint64_t targetLine) {
    ScanResult result{start.line, start.offset, start.offset, false, {}};
    if (targetLine <= start.line) {
        return result;
    }

    const auto width = unitWidth(encoding_);
    std::vector<std::byte> buffer(options_.scanChunkBytes + width);
    ScannerState state{start.line, start.offset, false};
    std::uint64_t position = start.offset;

    auto emitLine = [&](const std::uint64_t nextStart) {
        ++state.line;
        state.lineStart = nextStart;
        remember(state.line, state.lineStart);
        furthestIndexedLine_ = std::max(furthestIndexedLine_, state.line);
        furthestIndexedOffset_ = std::max(furthestIndexedOffset_, nextStart);
    };

    while (position < fileSize_ && state.line < targetLine) {
        const auto remaining = fileSize_ - position;
        auto request = static_cast<std::size_t>(std::min<std::uint64_t>(
            remaining, static_cast<std::uint64_t>(options_.scanChunkBytes)));
        request -= request % width;
        if (request == 0U) {
            request = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, width));
        }

        const auto read = cache.read(file, position, std::span<std::byte>(buffer.data(), request));
        if (!read) {
            result.error = read.error;
            return result;
        }
        if (read.bytesRead == 0U) {
            break;
        }
        const auto usable = read.bytesRead - (read.bytesRead % width);
        for (std::size_t local = 0U; local < usable && state.line < targetLine; local += width) {
            const auto absolute = position + static_cast<std::uint64_t>(local);
            const auto value = codeUnit(buffer.data() + local, encoding_);

            if (state.pendingCr) {
                if (value == 0x0AU) {
                    state.pendingCr = false;
                    emitLine(absolute + static_cast<std::uint64_t>(width));
                    continue;
                }
                state.pendingCr = false;
                emitLine(absolute);
                if (state.line >= targetLine) {
                    break;
                }
            }

            if (value == 0x0DU) {
                state.pendingCr = true;
            } else if (value == 0x0AU) {
                emitLine(absolute + static_cast<std::uint64_t>(width));
            }
        }
        position += static_cast<std::uint64_t>(usable);
        result.scannedUntil = position;
        if (usable == 0U) {
            break;
        }
    }

    if (state.line < targetLine && position >= fileSize_ && state.pendingCr) {
        state.pendingCr = false;
        emitLine(fileSize_);
    }

    result.line = state.line;
    result.lineStart = state.lineStart;
    result.scannedUntil = position;
    result.reachedEnd = position >= fileSize_;
    return result;
}

LineIndex::ScanResult LineIndex::scanToOffset(storage::RandomAccessFile& file,
                                              ViewCache& cache,
                                              const Checkpoint start,
                                              const std::uint64_t targetOffset) {
    ScanResult result{start.line, start.offset, start.offset, false, {}};
    const auto width = unitWidth(encoding_);
    std::vector<std::byte> buffer(options_.scanChunkBytes + width);
    ScannerState state{start.line, start.offset, false};
    std::uint64_t position = start.offset;

    auto emitLine = [&](const std::uint64_t nextStart) {
        if (targetOffset < nextStart) {
            return false;
        }
        ++state.line;
        state.lineStart = nextStart;
        remember(state.line, state.lineStart);
        furthestIndexedLine_ = std::max(furthestIndexedLine_, state.line);
        furthestIndexedOffset_ = std::max(furthestIndexedOffset_, nextStart);
        return true;
    };

    while (position < fileSize_ && position <= targetOffset) {
        const auto remaining = fileSize_ - position;
        const auto throughTarget = targetOffset - position + static_cast<std::uint64_t>(width);
        auto request64 = std::min<std::uint64_t>(remaining,
                                                static_cast<std::uint64_t>(options_.scanChunkBytes));
        request64 = std::min(request64, throughTarget);
        auto request = static_cast<std::size_t>(request64);
        request -= request % width;
        if (request == 0U) {
            request = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, width));
        }

        const auto read = cache.read(file, position, std::span<std::byte>(buffer.data(), request));
        if (!read) {
            result.error = read.error;
            return result;
        }
        if (read.bytesRead == 0U) {
            break;
        }
        const auto usable = read.bytesRead - (read.bytesRead % width);
        for (std::size_t local = 0U; local < usable; local += width) {
            const auto absolute = position + static_cast<std::uint64_t>(local);
            if (absolute > targetOffset) {
                break;
            }
            const auto value = codeUnit(buffer.data() + local, encoding_);

            if (state.pendingCr) {
                if (value == 0x0AU) {
                    state.pendingCr = false;
                    if (!emitLine(absolute + static_cast<std::uint64_t>(width))) {
                        result.line = state.line;
                        result.lineStart = state.lineStart;
                        result.scannedUntil = absolute;
                        return result;
                    }
                    continue;
                }
                state.pendingCr = false;
                if (!emitLine(absolute)) {
                    result.line = state.line;
                    result.lineStart = state.lineStart;
                    result.scannedUntil = absolute;
                    return result;
                }
            }

            if (value == 0x0DU) {
                state.pendingCr = true;
            } else if (value == 0x0AU) {
                if (!emitLine(absolute + static_cast<std::uint64_t>(width))) {
                    result.line = state.line;
                    result.lineStart = state.lineStart;
                    result.scannedUntil = absolute;
                    return result;
                }
            }
        }
        position += static_cast<std::uint64_t>(usable);
        result.scannedUntil = position;
        if (usable == 0U) {
            break;
        }
    }

    if (position >= fileSize_ && state.pendingCr && targetOffset >= fileSize_) {
        static_cast<void>(emitLine(fileSize_));
    }

    result.line = state.line;
    result.lineStart = state.lineStart;
    result.reachedEnd = position >= fileSize_;
    return result;
}

LineIndex::ScanResult LineIndex::scanToEnd(storage::RandomAccessFile& file,
                                           ViewCache& cache,
                                           const Checkpoint start) {
    ScanResult current{start.line, start.offset, start.offset, false, {}};
    constexpr std::uint64_t batchLines = 1'000'000U;
    while (!current.reachedEnd) {
        const auto target = current.line > std::numeric_limits<std::uint64_t>::max() - batchLines
                                ? std::numeric_limits<std::uint64_t>::max()
                                : current.line + batchLines;
        current = scanToLine(file, cache, {current.line, current.lineStart}, target);
        if (current.error || target == std::numeric_limits<std::uint64_t>::max()) {
            break;
        }
        if (current.line < target) {
            current.reachedEnd = true;
            break;
        }
    }
    return current;
}

LineOffsetResult LineIndex::lineStart(storage::RandomAccessFile& file,
                                      ViewCache& cache,
                                      const std::uint64_t oneBasedLine) {
    if (oneBasedLine == 0U) {
        return {std::nullopt, std::make_error_code(std::errc::invalid_argument)};
    }
    if (file.size() != fileSize_) {
        return {std::nullopt, std::make_error_code(std::errc::state_not_recoverable)};
    }

    const auto start = checkpointForLine(oneBasedLine);
    const auto scan = scanToLine(file, cache, start, oneBasedLine);
    if (scan.error) {
        return {std::nullopt, scan.error};
    }
    if (scan.line != oneBasedLine) {
        return {std::nullopt, {}};
    }
    return {scan.lineStart, {}};
}

OffsetLineResult LineIndex::lineAtOffset(storage::RandomAccessFile& file,
                                         ViewCache& cache,
                                         const std::uint64_t byteOffset) {
    if (byteOffset < contentStart_ || byteOffset > fileSize_) {
        return {std::nullopt, std::make_error_code(std::errc::invalid_argument)};
    }
    if (file.size() != fileSize_) {
        return {std::nullopt, std::make_error_code(std::errc::state_not_recoverable)};
    }

    const auto start = checkpointForOffset(byteOffset);
    const auto scan = scanToOffset(file, cache, start, byteOffset);
    if (scan.error) {
        return {std::nullopt, scan.error};
    }
    return {scan.line, {}};
}

LineCountResult LineIndex::lineCount(storage::RandomAccessFile& file, ViewCache& cache) {
    if (file.size() != fileSize_) {
        return {0U, std::make_error_code(std::errc::state_not_recoverable)};
    }
    if (fileSize_ == contentStart_) {
        return {0U, {}};
    }

    const auto start = checkpoints_.back();
    const auto scan = scanToEnd(file, cache, start);
    if (scan.error) {
        return {0U, scan.error};
    }
    return {scan.line, {}};
}

LineIndexStatistics LineIndex::statistics() const noexcept {
    return {checkpoints_.size(), furthestIndexedOffset_, furthestIndexedLine_};
}

std::uint64_t LineIndex::contentStart() const noexcept { return contentStart_; }

}
