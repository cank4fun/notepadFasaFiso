#include "notepadFasaFiso/search/SearchSource.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace nff::search {
namespace {

[[nodiscard]] constexpr unsigned char foldAscii(const unsigned char value) noexcept {
    if (value >= static_cast<unsigned char>('A') && value <= static_cast<unsigned char>('Z')) {
        return static_cast<unsigned char>(value +
                                          (static_cast<unsigned char>('a') -
                                           static_cast<unsigned char>('A')));
    }
    return value;
}

[[nodiscard]] constexpr bool byteEquals(const unsigned char left,
                                        const unsigned char right,
                                        const bool caseSensitive) noexcept {
    return caseSensitive ? left == right : foldAscii(left) == foldAscii(right);
}

[[nodiscard]] constexpr bool isWordByte(const unsigned char value) noexcept {
    if (value >= 0x80U) {
        return true;
    }
    return (value >= static_cast<unsigned char>('0') && value <= static_cast<unsigned char>('9')) ||
           (value >= static_cast<unsigned char>('A') && value <= static_cast<unsigned char>('Z')) ||
           (value >= static_cast<unsigned char>('a') && value <= static_cast<unsigned char>('z')) ||
           value == static_cast<unsigned char>('_');
}

struct LiteralPlan final {
    explicit LiteralPlan(const std::string_view value, const bool sensitive) noexcept
        : pattern(value), caseSensitive(sensitive) {
        skip.fill(pattern.size());
        if (pattern.size() <= 1U) {
            return;
        }
        for (std::size_t index = 0; index + 1U < pattern.size(); ++index) {
            const auto raw = static_cast<unsigned char>(pattern[index]);
            const auto key = caseSensitive ? raw : foldAscii(raw);
            skip[key] = pattern.size() - index - 1U;
        }
    }

    std::string_view pattern;
    bool caseSensitive{};
    std::array<std::size_t, 256> skip{};
};

[[nodiscard]] bool matchesAt(const std::string_view buffer,
                             const std::size_t offset,
                             const LiteralPlan& plan) noexcept {
    if (offset > buffer.size() || plan.pattern.size() > buffer.size() - offset) {
        return false;
    }
    for (std::size_t index = plan.pattern.size(); index > 0U; --index) {
        const auto candidate = static_cast<unsigned char>(buffer[offset + index - 1U]);
        const auto expected = static_cast<unsigned char>(plan.pattern[index - 1U]);
        if (!byteEquals(candidate, expected, plan.caseSensitive)) {
            return false;
        }
    }
    return true;
}

struct StreamingRangeFindResult final {
    std::optional<SearchMatch> match;
    std::uint64_t bytesScanned{};
    std::error_code error;
    bool cancelled{false};
};

[[nodiscard]] bool cancelled(const StreamingSearchOptions& options) noexcept {
    return options.cancelFlag != nullptr &&
           options.cancelFlag->load(std::memory_order_relaxed);
}

[[nodiscard]] bool acceptedWholeWord(const std::string_view view,
                                     const std::size_t offset,
                                     const std::size_t length,
                                     const bool wholeWord) noexcept {
    if (!wholeWord) {
        return true;
    }
    if (offset > 0U && isWordByte(static_cast<unsigned char>(view[offset - 1U]))) {
        return false;
    }
    const auto after = offset + length;
    return after >= view.size() ||
           !isWordByte(static_cast<unsigned char>(view[after]));
}

[[nodiscard]] StreamingRangeFindResult findForwardRange(
    ITextSearchSource& source,
    const std::string_view pattern,
    const std::uint64_t begin,
    const std::uint64_t end,
    const SearchOptions& options,
    const StreamingSearchOptions& streamingOptions) {
    StreamingRangeFindResult result;
    if (begin >= end) {
        return result;
    }

    const auto totalSize = source.size();
    const auto lookAhead = pattern.size();
    if (streamingOptions.chunkBytes >
        std::numeric_limits<std::size_t>::max() - lookAhead - 1U) {
        result.error = std::make_error_code(std::errc::value_too_large);
        return result;
    }

    const LiteralPlan plan(pattern, options.caseSensitive);
    std::vector<char> buffer(streamingOptions.chunkBytes + lookAhead + 1U);
    auto chunkStart = begin;

    while (chunkStart < end) {
        if (cancelled(streamingOptions)) {
            result.cancelled = true;
            result.error = std::make_error_code(std::errc::operation_canceled);
            return result;
        }

        const auto primaryBytes64 = std::min<std::uint64_t>(
            end - chunkStart, static_cast<std::uint64_t>(streamingOptions.chunkBytes));
        const auto primaryBytes = static_cast<std::size_t>(primaryBytes64);
        const bool hasPrefix = chunkStart > 0U;
        const auto readStart = hasPrefix ? chunkStart - 1U : chunkStart;
        const std::size_t prefixBytes = hasPrefix ? 1U : 0U;
        const auto requested64 = std::min<std::uint64_t>(
            totalSize - readStart,
            static_cast<std::uint64_t>(prefixBytes + primaryBytes + lookAhead));
        const auto requested = static_cast<std::size_t>(requested64);

        const auto read = source.read(readStart, std::span<char>(buffer.data(), requested));
        if (!read) {
            result.error = read.error;
            return result;
        }
        if (read.bytesRead < prefixBytes + primaryBytes) {
            result.error = std::make_error_code(std::errc::io_error);
            return result;
        }

        const std::string_view view(buffer.data(), read.bytesRead);
        const auto localPrimaryStart = prefixBytes;
        const auto localPrimaryEnd = prefixBytes + primaryBytes;
        auto local = localPrimaryStart;

        while (local < localPrimaryEnd && plan.pattern.size() <= view.size() - local) {
            const auto absoluteOffset = readStart + static_cast<std::uint64_t>(local);
            if (static_cast<std::uint64_t>(plan.pattern.size()) > end - absoluteOffset) {
                break;
            }

            if (matchesAt(view, local, plan)) {
                if (acceptedWholeWord(view, local, plan.pattern.size(), options.wholeWord)) {
                    result.bytesScanned +=
                        static_cast<std::uint64_t>(local - localPrimaryStart);
                    result.match = SearchMatch{
                        absoluteOffset, static_cast<std::uint64_t>(plan.pattern.size())};
                    return result;
                }
                ++local;
                continue;
            }

            const auto tail = static_cast<unsigned char>(view[local + plan.pattern.size() - 1U]);
            const auto key = plan.caseSensitive ? tail : foldAscii(tail);
            local += std::max<std::size_t>(1U, plan.skip[key]);
        }

        result.bytesScanned += primaryBytes64;
        chunkStart += primaryBytes64;
    }

    return result;
}

[[nodiscard]] StreamingRangeFindResult findBackwardRange(
    ITextSearchSource& source,
    const std::string_view pattern,
    const std::uint64_t begin,
    const std::uint64_t end,
    const SearchOptions& options,
    const StreamingSearchOptions& streamingOptions) {
    StreamingRangeFindResult result;
    if (begin >= end) {
        return result;
    }

    const auto totalSize = source.size();
    const auto lookAhead = pattern.size();
    if (streamingOptions.chunkBytes >
        std::numeric_limits<std::size_t>::max() - lookAhead - 1U) {
        result.error = std::make_error_code(std::errc::value_too_large);
        return result;
    }

    const LiteralPlan plan(pattern, options.caseSensitive);
    std::vector<char> buffer(streamingOptions.chunkBytes + lookAhead + 1U);
    auto chunkEnd = end;

    while (chunkEnd > begin) {
        if (cancelled(streamingOptions)) {
            result.cancelled = true;
            result.error = std::make_error_code(std::errc::operation_canceled);
            return result;
        }

        const auto primaryBytes64 = std::min<std::uint64_t>(
            chunkEnd - begin, static_cast<std::uint64_t>(streamingOptions.chunkBytes));
        const auto chunkStart = chunkEnd - primaryBytes64;
        const auto primaryBytes = static_cast<std::size_t>(primaryBytes64);
        const bool hasPrefix = chunkStart > 0U;
        const auto readStart = hasPrefix ? chunkStart - 1U : chunkStart;
        const std::size_t prefixBytes = hasPrefix ? 1U : 0U;
        const auto requested64 = std::min<std::uint64_t>(
            totalSize - readStart,
            static_cast<std::uint64_t>(prefixBytes + primaryBytes + lookAhead));
        const auto requested = static_cast<std::size_t>(requested64);

        const auto read = source.read(readStart, std::span<char>(buffer.data(), requested));
        if (!read) {
            result.error = read.error;
            return result;
        }
        if (read.bytesRead < prefixBytes + primaryBytes) {
            result.error = std::make_error_code(std::errc::io_error);
            return result;
        }

        const std::string_view view(buffer.data(), read.bytesRead);
        const auto localPrimaryStart = prefixBytes;
        const auto localPrimaryEnd = prefixBytes + primaryBytes;
        auto local = localPrimaryStart;
        std::optional<SearchMatch> lastMatch;

        while (local < localPrimaryEnd && plan.pattern.size() <= view.size() - local) {
            const auto absoluteOffset = readStart + static_cast<std::uint64_t>(local);
            if (static_cast<std::uint64_t>(plan.pattern.size()) <= end - absoluteOffset &&
                matchesAt(view, local, plan)) {
                if (acceptedWholeWord(view, local, plan.pattern.size(), options.wholeWord)) {
                    lastMatch = SearchMatch{
                        absoluteOffset, static_cast<std::uint64_t>(plan.pattern.size())};
                }
                ++local;
                continue;
            }

            const auto tail = static_cast<unsigned char>(view[local + plan.pattern.size() - 1U]);
            const auto key = plan.caseSensitive ? tail : foldAscii(tail);
            local += std::max<std::size_t>(1U, plan.skip[key]);
        }

        result.bytesScanned += primaryBytes64;
        if (lastMatch) {
            result.match = lastMatch;
            return result;
        }
        chunkEnd = chunkStart;
    }

    return result;
}

}

MemoryTextSearchSource::MemoryTextSearchSource(const std::string_view text) noexcept : text_(text) {}

std::uint64_t MemoryTextSearchSource::size() const noexcept {
    return static_cast<std::uint64_t>(text_.size());
}

SourceReadResult MemoryTextSearchSource::read(const std::uint64_t offset,
                                              const std::span<char> destination) {
    if (offset > static_cast<std::uint64_t>(text_.size())) {
        return {0U, std::make_error_code(std::errc::invalid_argument)};
    }
    const auto sourceOffset = static_cast<std::size_t>(offset);
    const auto available = text_.size() - sourceOffset;
    const auto count = std::min(available, destination.size());
    if (count > 0U) {
        std::memcpy(destination.data(), text_.data() + sourceOffset, count);
    }
    return {count, {}};
}

StreamingFindResult StreamingTextSearch::find(ITextSearchSource& source,
                                              const std::string_view pattern,
                                              const std::uint64_t startOffset,
                                              const SearchDirection direction,
                                              const SearchOptions& options,
                                              const StreamingSearchOptions& streamingOptions) {
    StreamingFindResult result;
    if (pattern.empty() || streamingOptions.chunkBytes == 0U) {
        result.error = std::make_error_code(std::errc::invalid_argument);
        return result;
    }
    if (options.kind != SearchKind::Literal) {
        result.error = std::make_error_code(std::errc::operation_not_supported);
        return result;
    }

    const auto totalSize = source.size();
    if (totalSize == 0U || static_cast<std::uint64_t>(pattern.size()) > totalSize) {
        return result;
    }

    const auto start = std::min(startOffset, totalSize);
    const auto runRange = [&](const std::uint64_t begin,
                              const std::uint64_t end) -> StreamingRangeFindResult {
        return direction == SearchDirection::Forward
                   ? findForwardRange(source, pattern, begin, end, options, streamingOptions)
                   : findBackwardRange(source, pattern, begin, end, options, streamingOptions);
    };

    const auto first = direction == SearchDirection::Forward ? runRange(start, totalSize)
                                                              : runRange(0U, start);
    result.bytesScanned = first.bytesScanned;
    result.error = first.error;
    result.cancelled = first.cancelled;
    result.match = first.match;
    if (!result || result.match || !options.wrapAround) {
        return result;
    }

    const bool hasWrappedRange = direction == SearchDirection::Forward ? start > 0U
                                                                        : start < totalSize;
    if (!hasWrappedRange) {
        return result;
    }

    const auto wrapped = direction == SearchDirection::Forward ? runRange(0U, start)
                                                                : runRange(start, totalSize);
    result.bytesScanned += wrapped.bytesScanned;
    result.error = wrapped.error;
    result.cancelled = wrapped.cancelled;
    result.match = wrapped.match;
    result.wrapped = wrapped.match.has_value();
    return result;
}

StreamingSearchResult StreamingTextSearch::findAll(ITextSearchSource& source,
                                                    const std::string_view pattern,
                                                    const SearchOptions& options,
                                                    const StreamingSearchOptions& streamingOptions) {
    StreamingSearchResult result;
    if (pattern.empty() || streamingOptions.chunkBytes == 0U) {
        result.error = std::make_error_code(std::errc::invalid_argument);
        return result;
    }
    if (options.kind != SearchKind::Literal) {
        result.error = std::make_error_code(std::errc::operation_not_supported);
        return result;
    }

    const auto totalSize = source.size();
    if (totalSize == 0U || static_cast<std::uint64_t>(pattern.size()) > totalSize) {
        return result;
    }

    const auto lookAhead = pattern.size();
    if (streamingOptions.chunkBytes > std::numeric_limits<std::size_t>::max() - lookAhead - 1U) {
        result.error = std::make_error_code(std::errc::value_too_large);
        return result;
    }

    const LiteralPlan plan(pattern, options.caseSensitive);
    std::vector<char> buffer(streamingOptions.chunkBytes + lookAhead + 1U);
    std::uint64_t chunkStart = 0U;

    while (chunkStart < totalSize) {
        if (streamingOptions.cancelFlag != nullptr &&
            streamingOptions.cancelFlag->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            result.error = std::make_error_code(std::errc::operation_canceled);
            return result;
        }

        const auto remaining = totalSize - chunkStart;
        const auto primaryBytes = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, streamingOptions.chunkBytes));
        const bool hasPrefix = chunkStart > 0U;
        const auto readStart = hasPrefix ? chunkStart - 1U : chunkStart;
        const std::size_t prefixBytes = hasPrefix ? 1U : 0U;
        const auto requested64 = std::min<std::uint64_t>(
            totalSize - readStart,
            static_cast<std::uint64_t>(prefixBytes + primaryBytes + lookAhead));
        const auto requested = static_cast<std::size_t>(requested64);

        const auto read = source.read(readStart, std::span<char>(buffer.data(), requested));
        if (!read) {
            result.error = read.error;
            return result;
        }
        if (read.bytesRead < prefixBytes + primaryBytes) {
            result.error = std::make_error_code(std::errc::io_error);
            return result;
        }

        const std::string_view view(buffer.data(), read.bytesRead);
        const auto localPrimaryStart = prefixBytes;
        const auto localPrimaryEnd = prefixBytes + primaryBytes;
        auto local = localPrimaryStart;

        while (local < localPrimaryEnd && plan.pattern.size() <= view.size() - local) {
            if (matchesAt(view, local, plan)) {
                bool accepted = true;
                if (options.wholeWord) {
                    if (local > 0U && isWordByte(static_cast<unsigned char>(view[local - 1U]))) {
                        accepted = false;
                    }
                    const auto after = local + plan.pattern.size();
                    if (after < view.size() &&
                        isWordByte(static_cast<unsigned char>(view[after]))) {
                        accepted = false;
                    }
                }

                if (accepted) {
                    if (result.matches.size() >= options.maxResults) {
                        result.truncated = true;
                        result.bytesScanned =
                            readStart + static_cast<std::uint64_t>(local - prefixBytes);
                        return result;
                    }
                    const auto absoluteOffset = readStart + static_cast<std::uint64_t>(local);
                    result.matches.push_back(
                        SearchMatch{absoluteOffset, static_cast<std::uint64_t>(plan.pattern.size())});
                    local += plan.pattern.size();
                    continue;
                }

                ++local;
                continue;
            }

            const auto tail = static_cast<unsigned char>(view[local + plan.pattern.size() - 1U]);
            const auto key = plan.caseSensitive ? tail : foldAscii(tail);
            local += std::max<std::size_t>(1U, plan.skip[key]);
        }

        chunkStart += static_cast<std::uint64_t>(primaryBytes);
        result.bytesScanned = chunkStart;
    }

    return result;
}

}
