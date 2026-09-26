#include "notepadFasaFiso/viewer/LargeFileViewer.hpp"

#include "notepadFasaFiso/encoding/TextCodec.hpp"
#include "notepadFasaFiso/platform/Platform.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <span>

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

[[nodiscard]] bool isNewlineUnit(const std::uint32_t value) noexcept {
    return value == 0x0AU || value == 0x0DU;
}

[[nodiscard]] constexpr bool isSearchWordUnit(const std::uint32_t value) noexcept {
    if (value >= 0x80U) {
        return true;
    }
    return (value >= static_cast<std::uint32_t>('0') &&
            value <= static_cast<std::uint32_t>('9')) ||
           (value >= static_cast<std::uint32_t>('A') &&
            value <= static_cast<std::uint32_t>('Z')) ||
           (value >= static_cast<std::uint32_t>('a') &&
            value <= static_cast<std::uint32_t>('z')) ||
           value == static_cast<std::uint32_t>('_');
}

[[nodiscard]] constexpr bool fixedWidthSearchEncoding(
    const encoding::Encoding value) noexcept {
    return value == encoding::Encoding::Utf16LE ||
           value == encoding::Encoding::Utf16BE ||
           value == encoding::Encoding::Utf32LE ||
           value == encoding::Encoding::Utf32BE;
}

}

std::uint64_t LargeFileViewer::bomBytes(const encoding::Encoding encoding,
                                        const bool hasBom) noexcept {
    if (!hasBom) {
        return 0U;
    }
    switch (encoding) {
    case encoding::Encoding::Utf8:
        return 3U;
    case encoding::Encoding::Utf16LE:
    case encoding::Encoding::Utf16BE:
        return 2U;
    case encoding::Encoding::Utf32LE:
    case encoding::Encoding::Utf32BE:
        return 4U;
    default:
        return 0U;
    }
}

encoding::DetectionResult LargeFileViewer::effectiveDetection() const noexcept {
    auto detection = profile_.encoding;
    if (overrideEncoding_.has_value()) {
        detection.encoding = *overrideEncoding_;
        detection.binaryLike = false;
        if (*overrideEncoding_ != profile_.encoding.encoding) {
            detection.hasBom = false;
        }
    }
    return detection;
}

void LargeFileViewer::configurePerformance(const PerformanceProfile requested) {
    requestedPerformance_ = requested;
    const auto available = platform::availablePhysicalMemoryBytes();
    resolvedPerformance_ = ViewCache::resolveProfile(requested, file_.size(), available);
    cache_.setPolicy(ViewCache::makePolicy(resolvedPerformance_, file_.size(), available));
    const auto detection = effectiveDetection();
    lineIndex_.reset(detection.encoding,
                     bomBytes(detection.encoding, detection.hasBom),
                     file_.size(),
                     LineIndex::makeOptions(resolvedPerformance_));
    textWindowPrimed_ = false;
}

ViewerOpenResult LargeFileViewer::open(const std::filesystem::path& path,
                                       const ViewerOpenOptions& options) {
    close();

    const auto inspected = core::FileSniffer::inspect(path, options.inspectOptions);
    if (!inspected) {
        return {inspected.error};
    }
    if (inspected.profile.encoding.binaryLike && !options.overrideEncoding.has_value()) {
        return {std::make_error_code(std::errc::operation_not_supported)};
    }
    if (inspected.profile.encoding.encoding == encoding::Encoding::Unknown8Bit &&
        !options.overrideEncoding.has_value()) {
        return {std::make_error_code(std::errc::operation_not_supported)};
    }

    const auto error = file_.open(path);
    if (error) {
        return {error};
    }

    const auto state = storage::FileStateTracker::capture(path);
    if (!state || !state.state.exists) {
        close();
        return {state.error ? state.error : std::make_error_code(std::errc::no_such_file_or_directory)};
    }

    profile_ = inspected.profile;
    openOptions_ = options;
    baselineState_ = state.state;
    overrideEncoding_ = options.overrideEncoding;
    configurePerformance(options.performance);
    return {};
}

ViewerOpenResult LargeFileViewer::openPrepared(
    const std::filesystem::path& path,
    const core::DocumentProfile& profile,
    const storage::FileState& inspectedState,
    const ViewerOpenOptions& options) {
    close();

    const auto currentState = storage::FileStateTracker::capture(path);
    const bool profileMatchesPath = !path.empty() && profile.path.lexically_normal() == path.lexically_normal();
    const bool canReuse = profileMatchesPath && currentState && currentState.state.exists &&
                          profile.fileSize == currentState.state.size &&
                          storage::FileStateTracker::compare(inspectedState, currentState) ==
                              storage::FileChangeState::Unchanged;
    if (!canReuse) {
        return open(path, options);
    }

    if (profile.encoding.binaryLike && !options.overrideEncoding.has_value()) {
        return {std::make_error_code(std::errc::operation_not_supported)};
    }
    if (profile.encoding.encoding == encoding::Encoding::Unknown8Bit &&
        !options.overrideEncoding.has_value()) {
        return {std::make_error_code(std::errc::operation_not_supported)};
    }

    const auto error = file_.open(path);
    if (error) {
        return {error};
    }

    const auto openedState = storage::FileStateTracker::capture(path);
    if (!openedState || !openedState.state.exists) {
        close();
        return {openedState.error ? openedState.error
                                  : std::make_error_code(std::errc::no_such_file_or_directory)};
    }
    if (storage::FileStateTracker::compare(currentState.state, openedState) !=
        storage::FileChangeState::Unchanged) {
        close();
        return open(path, options);
    }

    profile_ = profile;
    openOptions_ = options;
    baselineState_ = openedState.state;
    overrideEncoding_ = options.overrideEncoding;
    configurePerformance(options.performance);
    return {};
}

void LargeFileViewer::close() noexcept {
    file_.close();
    cache_.clear();
    profile_ = {};
    openOptions_ = {};
    baselineState_ = {};
    overrideEncoding_.reset();
    textWindowPrimed_ = false;
    requestedPerformance_ = PerformanceProfile::Automatic;
    resolvedPerformance_ = PerformanceProfile::MemorySaver;
}

bool LargeFileViewer::isOpen() const noexcept { return file_.isOpen(); }
std::uint64_t LargeFileViewer::size() const noexcept { return file_.size(); }

search::SourceReadResult LargeFileViewer::read(const std::uint64_t offset,
                                               const std::span<char> destination) {
    auto bytes = std::span<std::byte>(reinterpret_cast<std::byte*>(destination.data()),
                                      destination.size());
    const auto result = file_.readAt(offset, bytes);
    return {result.bytesRead, result.error};
}

RawWindowResult LargeFileViewer::readRaw(const std::uint64_t offset,
                                         const std::size_t maximumBytes) {
    return readRawWindow(offset, maximumBytes, true);
}

RawWindowResult LargeFileViewer::readRawWindow(const std::uint64_t offset,
                                                const std::size_t maximumBytes,
                                                const bool allowReadAhead) {
    RawWindowResult result;
    result.offset = offset;
    if (!isOpen()) {
        result.error = std::make_error_code(std::errc::bad_file_descriptor);
        return result;
    }
    if (offset > file_.size()) {
        result.error = std::make_error_code(std::errc::invalid_argument);
        return result;
    }

    const auto available = file_.size() - offset;
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
        available, static_cast<std::uint64_t>(maximumBytes)));
    result.bytes.resize(count);
    const auto readResult = cache_.read(file_, offset, result.bytes);
    if (!readResult) {
        result.error = readResult.error;
        result.bytes.clear();
        return result;
    }
    result.bytes.resize(readResult.bytesRead);

    if (allowReadAhead && cache_.policy().prefetchBlocks > 0U &&
        offset + readResult.bytesRead < file_.size()) {
        const auto next = offset + static_cast<std::uint64_t>(readResult.bytesRead);
        static_cast<void>(cache_.prefetch(file_, next, cache_.policy().prefetchBlocks));
    }
    return result;
}

TextWindowResult LargeFileViewer::readTextWindow(const std::uint64_t byteOffset,
                                                    const std::size_t maximumEncodedBytes) {
    TextWindowResult result;
    if (!isOpen()) {
        result.error = std::make_error_code(std::errc::bad_file_descriptor);
        return result;
    }
    if (maximumEncodedBytes == 0U) {
        result.error = std::make_error_code(std::errc::invalid_argument);
        return result;
    }

    const auto detection = effectiveDetection();
    if (detection.encoding == encoding::Encoding::Unknown8Bit ||
        detection.encoding == encoding::Encoding::Binary) {
        result.error = std::make_error_code(std::errc::operation_not_supported);
        return result;
    }

    const auto contentStart = bomBytes(detection.encoding, detection.hasBom);
    auto start = std::clamp<std::uint64_t>(byteOffset, contentStart, file_.size());
    const auto width = unitWidth(detection.encoding);
    if (width > 1U) {
        const auto relative = start - contentStart;
        start = contentStart + relative - (relative % static_cast<std::uint64_t>(width));
        if ((detection.encoding == encoding::Encoding::Utf16LE ||
             detection.encoding == encoding::Encoding::Utf16BE) &&
            start >= contentStart + 2U && start + 1U < file_.size()) {
            std::array<std::byte, 2> current{};
            const auto currentRead = cache_.read(file_, start, current);
            if (!currentRead || currentRead.bytesRead != current.size()) {
                result.error = currentRead.error ? currentRead.error
                                                 : std::make_error_code(std::errc::io_error);
                return result;
            }
            const auto currentUnit = codeUnit(current.data(), detection.encoding);
            if (currentUnit >= 0xDC00U && currentUnit <= 0xDFFFU) {
                std::array<std::byte, 2> previous{};
                const auto previousRead = cache_.read(file_, start - 2U, previous);
                if (!previousRead || previousRead.bytesRead != previous.size()) {
                    result.error = previousRead.error ? previousRead.error
                                                      : std::make_error_code(std::errc::io_error);
                    return result;
                }
                const auto previousUnit = codeUnit(previous.data(), detection.encoding);
                if (previousUnit >= 0xD800U && previousUnit <= 0xDBFFU) {
                    start -= 2U;
                }
            }
        }
    } else if (detection.encoding == encoding::Encoding::Utf8) {
        std::array<std::byte, 1> current{};
        for (std::size_t skipped = 0U; start < file_.size() && skipped < 3U; ++skipped) {
            const auto readResult = cache_.read(file_, start, current);
            if (!readResult || readResult.bytesRead != 1U) {
                result.error = readResult.error ? readResult.error
                                                : std::make_error_code(std::errc::io_error);
                return result;
            }
            const auto value = std::to_integer<unsigned char>(current[0]);
            if ((value & 0xC0U) != 0x80U) {
                break;
            }
            ++start;
        }
    }

    auto raw = readRawWindow(start, maximumEncodedBytes, textWindowPrimed_);
    if (!raw) {
        result.error = raw.error;
        return result;
    }
    if (width > 1U && !raw.bytes.empty()) {
        raw.bytes.resize(raw.bytes.size() - (raw.bytes.size() % width));
    }

    encoding::DecodeOptions decodeOptions;
    decodeOptions.allowTruncatedTail = start + raw.bytes.size() < file_.size();
    const auto decoded = encoding::TextCodec::decode(raw.bytes,
                                                     detection.encoding,
                                                     false,
                                                     decodeOptions);
    if (!decoded) {
        result.error = decoded.error;
        return result;
    }

    result.text = decoded.text;
    textWindowPrimed_ = true;
    result.firstLine = start == contentStart ? 1U : 0U;
    result.byteStart = start;
    result.byteEnd = start + raw.bytes.size();
    return result;
}

LineOffsetResult LargeFileViewer::lineStart(const std::uint64_t oneBasedLine) {
    if (!isOpen()) {
        return {std::nullopt, std::make_error_code(std::errc::bad_file_descriptor)};
    }
    return lineIndex_.lineStart(file_, cache_, oneBasedLine);
}

OffsetLineResult LargeFileViewer::lineAtOffset(const std::uint64_t byteOffset) {
    if (!isOpen()) {
        return {std::nullopt, std::make_error_code(std::errc::bad_file_descriptor)};
    }
    return lineIndex_.lineAtOffset(file_, cache_, byteOffset);
}

LineCountResult LargeFileViewer::lineCount() {
    if (!isOpen()) {
        return {0U, std::make_error_code(std::errc::bad_file_descriptor)};
    }
    return lineIndex_.lineCount(file_, cache_);
}

TextWindowResult LargeFileViewer::readLines(const std::uint64_t firstLine,
                                            const std::uint64_t lineCountValue,
                                            const std::size_t maximumDecodedBytes) {
    TextWindowResult result;
    result.firstLine = firstLine;
    if (!isOpen()) {
        result.error = std::make_error_code(std::errc::bad_file_descriptor);
        return result;
    }
    if (firstLine == 0U || lineCountValue == 0U || maximumDecodedBytes == 0U) {
        result.error = std::make_error_code(std::errc::invalid_argument);
        return result;
    }

    const auto start = lineStart(firstLine);
    if (!start) {
        result.error = start.error;
        return result;
    }
    if (!start.offset.has_value()) {
        return result;
    }
    result.byteStart = *start.offset;

    std::uint64_t requestedEndLine = std::numeric_limits<std::uint64_t>::max();
    if (firstLine <= std::numeric_limits<std::uint64_t>::max() - lineCountValue) {
        requestedEndLine = firstLine + lineCountValue;
    }

    const auto end = lineStart(requestedEndLine);
    if (!end) {
        result.error = end.error;
        return result;
    }
    result.byteEnd = end.offset.value_or(file_.size());
    if (result.byteEnd < result.byteStart) {
        result.error = std::make_error_code(std::errc::state_not_recoverable);
        return result;
    }

    const auto byteCount64 = result.byteEnd - result.byteStart;
    if (byteCount64 > static_cast<std::uint64_t>(maximumDecodedBytes)) {
        result.error = std::make_error_code(std::errc::value_too_large);
        return result;
    }
    const auto byteCount = static_cast<std::size_t>(byteCount64);
    auto raw = readRaw(result.byteStart, byteCount);
    if (!raw) {
        result.error = raw.error;
        return result;
    }

    const auto detection = effectiveDetection();
    encoding::DecodeOptions decodeOptions;
    decodeOptions.allowTruncatedTail = false;
    const auto decoded = encoding::TextCodec::decode(raw.bytes,
                                                     detection.encoding,
                                                     false,
                                                     decodeOptions);
    if (!decoded) {
        result.error = decoded.error;
        return result;
    }
    result.text = decoded.text;
    return result;
}

TailWindowResult LargeFileViewer::readTail(const std::uint64_t lineCountValue,
                                           const std::size_t maximumDecodedBytes) {
    TailWindowResult result;
    if (!isOpen()) {
        result.error = std::make_error_code(std::errc::bad_file_descriptor);
        return result;
    }
    if (lineCountValue == 0U || maximumDecodedBytes == 0U) {
        result.error = std::make_error_code(std::errc::invalid_argument);
        return result;
    }

    const auto detection = effectiveDetection();
    const auto width = unitWidth(detection.encoding);
    const auto contentStart = bomBytes(detection.encoding, detection.hasBom);
    result.byteEnd = file_.size();
    if (result.byteEnd <= contentStart) {
        result.byteStart = contentStart;
        return result;
    }

    const auto payloadBytes = result.byteEnd - contentStart;
    const auto alignedPayload = payloadBytes - (payloadBytes % static_cast<std::uint64_t>(width));
    const auto alignedEnd = contentStart + alignedPayload;
    if (alignedEnd == contentStart) {
        result.byteStart = contentStart;
        return result;
    }

    std::array<std::byte, 4> lastUnit{};
    const auto lastRead = cache_.read(file_, alignedEnd - static_cast<std::uint64_t>(width),
                                      std::span<std::byte>(lastUnit.data(), width));
    if (!lastRead || lastRead.bytesRead != width) {
        result.error = lastRead.error ? lastRead.error : std::make_error_code(std::errc::io_error);
        return result;
    }

    const bool endsWithNewline = isNewlineUnit(codeUnit(lastUnit.data(), detection.encoding));
    std::uint64_t separatorsNeeded = lineCountValue;
    if (endsWithNewline && separatorsNeeded != std::numeric_limits<std::uint64_t>::max()) {
        ++separatorsNeeded;
    }

    constexpr std::size_t scanBlockBytes = 1U * 1024U * 1024U;
    std::vector<std::byte> buffer(scanBlockBytes + width);
    std::uint64_t foundSeparators = 0U;
    std::uint64_t position = alignedEnd;
    std::uint64_t start = contentStart;
    bool skipCr = false;
    bool done = false;

    while (position > contentStart && !done) {
        const auto available = position - contentStart;
        auto request = static_cast<std::size_t>(std::min<std::uint64_t>(
            available, static_cast<std::uint64_t>(scanBlockBytes)));
        request -= request % width;
        if (request == 0U) {
            request = width;
        }
        const auto blockStart = position - static_cast<std::uint64_t>(request);
        const auto read = cache_.read(file_, blockStart,
                                      std::span<std::byte>(buffer.data(), request));
        if (!read || read.bytesRead != request) {
            result.error = read.error ? read.error : std::make_error_code(std::errc::io_error);
            return result;
        }

        for (std::size_t local = request; local > 0U; local -= width) {
            const auto unitOffset = local - width;
            const auto absolute = blockStart + static_cast<std::uint64_t>(unitOffset);
            const auto value = codeUnit(buffer.data() + unitOffset, detection.encoding);

            if (value == 0x0AU) {
                ++foundSeparators;
                skipCr = true;
                if (foundSeparators >= separatorsNeeded) {
                    start = absolute + static_cast<std::uint64_t>(width);
                    done = true;
                    break;
                }
                continue;
            }

            if (value == 0x0DU) {
                if (skipCr) {
                    skipCr = false;
                    continue;
                }
                ++foundSeparators;
                if (foundSeparators >= separatorsNeeded) {
                    start = absolute + static_cast<std::uint64_t>(width);
                    done = true;
                    break;
                }
                continue;
            }

            skipCr = false;
        }
        position = blockStart;
        if (!done && result.byteEnd - blockStart >
                         static_cast<std::uint64_t>(maximumDecodedBytes)) {
            result.error = std::make_error_code(std::errc::value_too_large);
            return result;
        }
    }

    result.byteStart = start;
    const auto bytesToDecode64 = result.byteEnd - result.byteStart;
    if (bytesToDecode64 > static_cast<std::uint64_t>(maximumDecodedBytes)) {
        result.error = std::make_error_code(std::errc::value_too_large);
        return result;
    }

    const auto bytesToDecode = static_cast<std::size_t>(bytesToDecode64);
    auto raw = readRaw(result.byteStart, bytesToDecode);
    if (!raw) {
        result.error = raw.error;
        return result;
    }

    encoding::DecodeOptions decodeOptions;
    decodeOptions.allowTruncatedTail = true;
    const auto decoded = encoding::TextCodec::decode(raw.bytes,
                                                     detection.encoding,
                                                     false,
                                                     decodeOptions);
    if (!decoded) {
        result.error = decoded.error;
        return result;
    }
    result.text = decoded.text;
    return result;
}

ViewerRefreshResult LargeFileViewer::refresh() {
    ViewerRefreshResult result;
    result.previousSize = file_.size();
    result.currentSize = file_.size();
    if (!isOpen()) {
        result.error = std::make_error_code(std::errc::bad_file_descriptor);
        return result;
    }

    const auto current = storage::FileStateTracker::capture(profile_.path);
    const auto change = storage::FileStateTracker::compare(baselineState_, current);
    if (change == storage::FileChangeState::Unchanged) {
        return result;
    }
    if (change == storage::FileChangeState::Deleted) {
        result.kind = ViewerRefreshKind::Deleted;
        result.currentSize = 0U;
        return result;
    }
    if (change == storage::FileChangeState::Inaccessible || !current) {
        result.kind = ViewerRefreshKind::Inaccessible;
        result.error = current.error ? current.error : std::make_error_code(std::errc::io_error);
        return result;
    }

    storage::RandomAccessFile candidate;
    const auto openError = candidate.open(profile_.path);
    if (openError) {
        result.kind = ViewerRefreshKind::Inaccessible;
        result.error = openError;
        return result;
    }

    const auto previousIdentity = file_.identity();
    const auto nextIdentity = candidate.identity();
    const bool replaced = previousIdentity.valid && nextIdentity.valid &&
                          previousIdentity != nextIdentity;
    result.currentSize = candidate.size();

    if (replaced) {
        const auto inspected = core::FileSniffer::inspect(profile_.path, openOptions_.inspectOptions);
        if (!inspected) {
            result.error = inspected.error;
            return result;
        }
        if (inspected.profile.encoding.binaryLike && !overrideEncoding_.has_value()) {
            result.error = std::make_error_code(std::errc::operation_not_supported);
            return result;
        }
        if (inspected.profile.encoding.encoding == encoding::Encoding::Unknown8Bit &&
            !overrideEncoding_.has_value()) {
            result.error = std::make_error_code(std::errc::operation_not_supported);
            return result;
        }
        file_ = std::move(candidate);
        profile_ = inspected.profile;
        baselineState_ = current.state;
        configurePerformance(requestedPerformance_);
        result.kind = ViewerRefreshKind::Replaced;
        return result;
    }

    if (result.currentSize > result.previousSize) {
        file_ = std::move(candidate);
        profile_.fileSize = static_cast<std::uintmax_t>(file_.size());
        baselineState_ = current.state;
        configurePerformance(requestedPerformance_);
        result.kind = ViewerRefreshKind::Grown;
        return result;
    }

    const auto inspected = core::FileSniffer::inspect(profile_.path, openOptions_.inspectOptions);
    if (!inspected) {
        result.error = inspected.error;
        return result;
    }
    if (inspected.profile.encoding.binaryLike && !overrideEncoding_.has_value()) {
        result.error = std::make_error_code(std::errc::operation_not_supported);
        return result;
    }
    if (inspected.profile.encoding.encoding == encoding::Encoding::Unknown8Bit &&
        !overrideEncoding_.has_value()) {
        result.error = std::make_error_code(std::errc::operation_not_supported);
        return result;
    }

    file_ = std::move(candidate);
    profile_ = inspected.profile;
    baselineState_ = current.state;
    configurePerformance(requestedPerformance_);
    result.kind = result.currentSize < result.previousSize ? ViewerRefreshKind::Truncated
                                                          : ViewerRefreshKind::Modified;
    return result;
}

bool LargeFileViewer::fixedWidthWholeWordMatch(
    const search::SearchMatch& match,
    const encoding::Encoding encoding,
    const std::uint64_t contentStart,
    std::error_code& error) {
    error.clear();
    const auto width = unitWidth(encoding);
    if (width <= 1U || match.offset < contentStart ||
        (match.offset - contentStart) % static_cast<std::uint64_t>(width) != 0U ||
        match.length % static_cast<std::uint64_t>(width) != 0U ||
        match.length > file_.size() - std::min(match.offset, file_.size())) {
        return false;
    }

    const auto wordAt = [this, encoding, width, &error](const std::uint64_t offset) {
        std::array<std::byte, 4> bytes{};
        const auto read = cache_.read(
            file_, offset, std::span<std::byte>(bytes.data(), width));
        if (!read || read.bytesRead != width) {
            error = read.error ? read.error : std::make_error_code(std::errc::io_error);
            return false;
        }
        return isSearchWordUnit(codeUnit(bytes.data(), encoding));
    };

    if (match.offset > contentStart) {
        if (match.offset - contentStart < static_cast<std::uint64_t>(width)) {
            return false;
        }
        if (wordAt(match.offset - static_cast<std::uint64_t>(width))) {
            return false;
        }
        if (error) {
            return false;
        }
    }

    const auto after = match.offset + match.length;
    if (after < file_.size()) {
        if (file_.size() - after < static_cast<std::uint64_t>(width)) {
            return false;
        }
        if (wordAt(after)) {
            return false;
        }
        if (error) {
            return false;
        }
    }
    return true;
}

search::StreamingFindResult LargeFileViewer::search(
    const std::string_view pattern,
    const std::uint64_t startOffset,
    const search::SearchDirection direction,
    const search::SearchOptions& options,
    const search::StreamingSearchOptions& streamingOptions) {
    search::StreamingFindResult result;
    if (!isOpen()) {
        result.error = std::make_error_code(std::errc::bad_file_descriptor);
        return result;
    }
    if (options.kind != search::SearchKind::Literal) {
        result.error = std::make_error_code(std::errc::operation_not_supported);
        return result;
    }
    if (!byteSearchCompatible()) {
        result.error = std::make_error_code(std::errc::operation_not_supported);
        return result;
    }

    const auto detection = effectiveDetection();
    const auto fileEncoding = detection.encoding;
    std::string encodedPattern;
    std::string_view searchPattern = pattern;
    if (fileEncoding == encoding::Encoding::Windows1252 ||
        fileEncoding == encoding::Encoding::Windows1254 ||
        fixedWidthSearchEncoding(fileEncoding)) {
        const auto encoded = encoding::TextCodec::encode(pattern, fileEncoding, false);
        if (!encoded) {
            result.error = encoded.error;
            return result;
        }
        encodedPattern.assign(reinterpret_cast<const char*>(encoded.bytes.data()),
                              encoded.bytes.size());
        searchPattern = encodedPattern;
    }

    if (!fixedWidthSearchEncoding(fileEncoding)) {
        return search::StreamingTextSearch::find(
            *this, searchPattern, startOffset, direction, options, streamingOptions);
    }

    const auto width = static_cast<std::uint64_t>(unitWidth(fileEncoding));
    const auto contentStart = bomBytes(fileEncoding, detection.hasBom);
    if (searchPattern.empty() || searchPattern.size() % static_cast<std::size_t>(width) != 0U) {
        result.error = std::make_error_code(std::errc::invalid_argument);
        return result;
    }

    auto rawOptions = options;
    rawOptions.wholeWord = false;
    rawOptions.wrapAround = false;

    const auto accepted = [this, fileEncoding, contentStart, width, &options](
                              const search::SearchMatch& match,
                              std::error_code& error) {
        if (match.offset < contentStart ||
            (match.offset - contentStart) % width != 0U ||
            match.length % width != 0U) {
            return false;
        }
        if (!options.wholeWord) {
            return true;
        }
        return fixedWidthWholeWordMatch(match, fileEncoding, contentStart, error);
    };

    const auto run = [&](std::uint64_t cursor,
                         const search::SearchDirection phaseDirection,
                         const std::optional<std::pair<std::uint64_t, std::uint64_t>> bounds)
        -> search::StreamingFindResult {
        search::StreamingFindResult phase;
        while (true) {
            const auto raw = search::StreamingTextSearch::find(
                *this, searchPattern, cursor, phaseDirection, rawOptions, streamingOptions);
            phase.bytesScanned += raw.bytesScanned;
            phase.error = raw.error;
            phase.cancelled = raw.cancelled;
            if (!raw || !raw.match) {
                return phase;
            }
            if (bounds && (raw.match->offset < bounds->first || raw.match->offset >= bounds->second)) {
                return phase;
            }
            std::error_code boundaryError;
            if (accepted(*raw.match, boundaryError)) {
                phase.match = raw.match;
                return phase;
            }
            if (boundaryError) {
                phase.error = boundaryError;
                return phase;
            }
            if (phaseDirection == search::SearchDirection::Forward) {
                if (raw.match->offset == std::numeric_limits<std::uint64_t>::max()) {
                    return phase;
                }
                cursor = raw.match->offset + 1U;
            } else {
                if (raw.match->offset == 0U) {
                    return phase;
                }
                cursor = raw.match->offset;
            }
        }
    };

    const auto total = file_.size();
    const auto start = std::clamp<std::uint64_t>(startOffset, contentStart, total);
    const auto firstBounds = direction == search::SearchDirection::Forward
                                 ? std::optional{std::pair{start, total}}
                                 : std::optional{std::pair{contentStart, start}};
    auto first = run(start, direction, firstBounds);
    result.bytesScanned = first.bytesScanned;
    result.error = first.error;
    result.cancelled = first.cancelled;
    result.match = first.match;
    if (!result || result.match || !options.wrapAround) {
        return result;
    }

    const bool hasWrappedRange = direction == search::SearchDirection::Forward
                                     ? start > contentStart
                                     : start < total;
    if (!hasWrappedRange) {
        return result;
    }
    const auto wrappedStart = direction == search::SearchDirection::Forward ? contentStart : total;
    const auto wrappedBounds = direction == search::SearchDirection::Forward
                                   ? std::optional{std::pair{contentStart, start}}
                                   : std::optional{std::pair{start, total}};
    auto wrapped = run(wrappedStart, direction, wrappedBounds);
    result.bytesScanned += wrapped.bytesScanned;
    result.error = wrapped.error;
    result.cancelled = wrapped.cancelled;
    result.match = wrapped.match;
    result.wrapped = wrapped.match.has_value();
    return result;
}

search::StreamingSearchResult LargeFileViewer::searchAll(
    const std::string_view pattern,
    const search::SearchOptions& options,
    const search::StreamingSearchOptions& streamingOptions) {
    search::StreamingSearchResult result;
    if (!isOpen()) {
        result.error = std::make_error_code(std::errc::bad_file_descriptor);
        return result;
    }
    if (options.kind != search::SearchKind::Literal) {
        result.error = std::make_error_code(std::errc::operation_not_supported);
        return result;
    }
    if (!byteSearchCompatible()) {
        result.error = std::make_error_code(std::errc::operation_not_supported);
        return result;
    }

    const auto detection = effectiveDetection();
    const auto fileEncoding = detection.encoding;
    std::string encodedPattern;
    std::string_view searchPattern = pattern;
    if (fileEncoding == encoding::Encoding::Windows1252 ||
        fileEncoding == encoding::Encoding::Windows1254 ||
        fixedWidthSearchEncoding(fileEncoding)) {
        const auto encoded = encoding::TextCodec::encode(pattern, fileEncoding, false);
        if (!encoded) {
            result.error = encoded.error;
            return result;
        }
        encodedPattern.assign(reinterpret_cast<const char*>(encoded.bytes.data()),
                              encoded.bytes.size());
        searchPattern = encodedPattern;
    }

    if (!fixedWidthSearchEncoding(fileEncoding)) {
        return search::StreamingTextSearch::findAll(
            *this, searchPattern, options, streamingOptions);
    }

    const auto width = static_cast<std::uint64_t>(unitWidth(fileEncoding));
    const auto contentStart = bomBytes(fileEncoding, detection.hasBom);
    auto rawOptions = options;
    rawOptions.wholeWord = false;
    rawOptions.wrapAround = false;
    rawOptions.maxResults = 1U;

    auto cursor = contentStart;
    while (cursor < file_.size()) {
        const auto raw = search::StreamingTextSearch::find(
            *this, searchPattern, cursor, search::SearchDirection::Forward,
            rawOptions, streamingOptions);
        result.bytesScanned += raw.bytesScanned;
        if (!raw) {
            result.error = raw.error;
            result.cancelled = raw.cancelled;
            return result;
        }
        if (!raw.match) {
            return result;
        }

        const auto aligned = raw.match->offset >= contentStart &&
                             (raw.match->offset - contentStart) % width == 0U &&
                             raw.match->length % width == 0U;
        std::error_code boundaryError;
        const bool wholeWordAccepted = !options.wholeWord ||
                                       (aligned && fixedWidthWholeWordMatch(
                                                       *raw.match, fileEncoding,
                                                       contentStart, boundaryError));
        if (boundaryError) {
            result.error = boundaryError;
            return result;
        }
        if (aligned && wholeWordAccepted) {
            if (result.matches.size() >= options.maxResults) {
                result.truncated = true;
                return result;
            }
            result.matches.push_back(*raw.match);
            if (raw.match->length > std::numeric_limits<std::uint64_t>::max() - raw.match->offset) {
                return result;
            }
            cursor = raw.match->offset + std::max<std::uint64_t>(raw.match->length, width);
        } else {
            if (raw.match->offset == std::numeric_limits<std::uint64_t>::max()) {
                return result;
            }
            cursor = raw.match->offset + 1U;
        }
    }
    return result;
}

void LargeFileViewer::setPerformanceProfile(const PerformanceProfile profile) {
    if (!isOpen()) {
        requestedPerformance_ = profile;
        return;
    }
    configurePerformance(profile);
}

PerformanceProfile LargeFileViewer::performanceProfile() const noexcept {
    return resolvedPerformance_;
}

CacheStatistics LargeFileViewer::cacheStatistics() const noexcept {
    return cache_.statistics();
}

LineIndexStatistics LargeFileViewer::lineIndexStatistics() const noexcept {
    return lineIndex_.statistics();
}

std::size_t LargeFileViewer::recommendedInitialTextWindowBytes() const noexcept {
    constexpr std::size_t fastInitialBytes = 512U * 1024U;
    constexpr std::size_t memorySaverInitialBytes = 256U * 1024U;
    constexpr std::size_t giantLineInitialBytes = 128U * 1024U;
    constexpr std::size_t giantLineThreshold = 256U * 1024U;
    if (profile_.longestSampledLine >= giantLineThreshold) {
        return giantLineInitialBytes;
    }
    return resolvedPerformance_ == PerformanceProfile::Fast
               ? fastInitialBytes
               : memorySaverInitialBytes;
}

const core::DocumentProfile& LargeFileViewer::profile() const noexcept { return profile_; }

bool LargeFileViewer::byteSearchCompatible() const noexcept {
    switch (effectiveDetection().encoding) {
    case encoding::Encoding::Ascii:
    case encoding::Encoding::Utf8:
    case encoding::Encoding::Utf16LE:
    case encoding::Encoding::Utf16BE:
    case encoding::Encoding::Utf32LE:
    case encoding::Encoding::Utf32BE:
    case encoding::Encoding::Windows1252:
    case encoding::Encoding::Windows1254:
    case encoding::Encoding::Unknown8Bit:
        return true;
    default:
        return false;
    }
}

}
