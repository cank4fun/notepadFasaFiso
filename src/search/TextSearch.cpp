#include "notepadFasaFiso/search/TextSearch.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <regex>

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

[[nodiscard]] bool hasWholeWordBoundaries(const std::string_view text,
                                          const std::size_t offset,
                                          const std::size_t length) noexcept {
    if (offset > 0U && isWordByte(static_cast<unsigned char>(text[offset - 1U]))) {
        return false;
    }

    const auto end = offset + length;
    return end >= text.size() || !isWordByte(static_cast<unsigned char>(text[end]));
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

[[nodiscard]] std::optional<std::size_t> findLiteralForward(const std::string_view text,
                                                            const LiteralPlan& plan,
                                                            const std::size_t begin,
                                                            const std::size_t endExclusive,
                                                            const bool wholeWord) {
    if (plan.pattern.empty() || begin > text.size()) {
        return std::nullopt;
    }

    const auto boundedEnd = std::min(endExclusive, text.size());
    if (begin >= boundedEnd || plan.pattern.size() > boundedEnd - begin) {
        return std::nullopt;
    }

    std::size_t cursor = begin;
    const auto lastStart = boundedEnd - plan.pattern.size();
    while (cursor <= lastStart) {
        std::size_t patternIndex = plan.pattern.size();
        while (patternIndex > 0U) {
            const auto candidate = static_cast<unsigned char>(text[cursor + patternIndex - 1U]);
            const auto expected = static_cast<unsigned char>(plan.pattern[patternIndex - 1U]);
            if (!byteEquals(candidate, expected, plan.caseSensitive)) {
                break;
            }
            --patternIndex;
        }

        if (patternIndex == 0U) {
            if (!wholeWord || hasWholeWordBoundaries(text, cursor, plan.pattern.size())) {
                return cursor;
            }
            ++cursor;
            continue;
        }

        const auto tail = static_cast<unsigned char>(text[cursor + plan.pattern.size() - 1U]);
        const auto key = plan.caseSensitive ? tail : foldAscii(tail);
        cursor += std::max<std::size_t>(1U, plan.skip[key]);
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> findLiteralBackward(const std::string_view text,
                                                             const LiteralPlan& plan,
                                                             const std::size_t beginInclusive,
                                                             const std::size_t endExclusive,
                                                             const bool wholeWord) {
    if (plan.pattern.empty() || beginInclusive >= endExclusive || beginInclusive >= text.size()) {
        return std::nullopt;
    }

    const auto boundedEnd = std::min(endExclusive, text.size());
    auto cursor = beginInclusive;
    std::optional<std::size_t> last;
    while (cursor < boundedEnd) {
        const auto found = findLiteralForward(text, plan, cursor, boundedEnd, wholeWord);
        if (!found) {
            break;
        }
        last = *found;
        if (*found == std::numeric_limits<std::size_t>::max()) {
            break;
        }
        cursor = *found + 1U;
    }
    return last;
}

[[nodiscard]] std::regex_constants::syntax_option_type regexFlags(const SearchOptions& options) {
    auto flags = std::regex_constants::ECMAScript | std::regex_constants::optimize;
    if (!options.caseSensitive) {
        flags |= std::regex_constants::icase;
    }
    return flags;
}

using RegexMatch = std::match_results<std::string_view::const_iterator>;

struct RegexAnchorInfo final {
    bool hasBegin{};
    bool hasEnd{};
};

[[nodiscard]] RegexAnchorInfo inspectRegexAnchors(const std::string_view pattern) noexcept {
    RegexAnchorInfo info;
    bool inCharacterClass = false;
    bool escaped = false;

    for (const char value : pattern) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (value == '\\') {
            escaped = true;
            continue;
        }
        if (value == '[' && !inCharacterClass) {
            inCharacterClass = true;
            continue;
        }
        if (value == ']' && inCharacterClass) {
            inCharacterClass = false;
            continue;
        }
        if (inCharacterClass) {
            continue;
        }
        if (value == '^') {
            info.hasBegin = true;
        } else if (value == '$') {
            info.hasEnd = true;
        }
    }
    return info;
}

[[nodiscard]] std::string disableRegexAnchors(const std::string_view pattern,
                                              const bool disableBegin,
                                              const bool disableEnd) {
    std::string output;
    output.reserve(pattern.size() + 16U);
    bool inCharacterClass = false;
    bool escaped = false;

    for (const char value : pattern) {
        if (escaped) {
            output.push_back(value);
            escaped = false;
            continue;
        }
        if (value == '\\') {
            output.push_back(value);
            escaped = true;
            continue;
        }
        if (value == '[' && !inCharacterClass) {
            inCharacterClass = true;
            output.push_back(value);
            continue;
        }
        if (value == ']' && inCharacterClass) {
            inCharacterClass = false;
            output.push_back(value);
            continue;
        }
        if (!inCharacterClass && ((disableBegin && value == '^') ||
                                  (disableEnd && value == '$'))) {
            output.append("(?!)");
            continue;
        }
        output.push_back(value);
    }
    return output;
}

struct RegexPlan final {
    RegexPlan(const std::string_view pattern, const SearchOptions& options)
        : source(pattern), anchors(inspectRegexAnchors(pattern)),
          expression(std::string(pattern), regexFlags(options)) {
        if (anchors.hasBegin) {
            withoutBegin.emplace(disableRegexAnchors(pattern, true, false), regexFlags(options));
        }
        if (anchors.hasEnd) {
            withoutEnd.emplace(disableRegexAnchors(pattern, false, true), regexFlags(options));
        }
        if (anchors.hasBegin && anchors.hasEnd) {
            withoutBeginEnd.emplace(disableRegexAnchors(pattern, true, true), regexFlags(options));
        }
    }

    std::string source;
    RegexAnchorInfo anchors;
    std::regex expression;
    std::optional<std::regex> withoutBegin;
    std::optional<std::regex> withoutEnd;
    std::optional<std::regex> withoutBeginEnd;
};

[[nodiscard]] bool hasPortableAbsoluteAnchors(const std::string_view text,
                                              const RegexPlan& plan,
                                              const std::size_t offset,
                                              const std::size_t length) {
    const bool disableBegin = plan.anchors.hasBegin && offset != 0U;
    const bool disableEnd = plan.anchors.hasEnd &&
                            (offset > text.size() || length != text.size() - offset);
    if (!disableBegin && !disableEnd) {
        return true;
    }

    const std::regex* validationExpression = nullptr;
    if (disableBegin && disableEnd) {
        validationExpression = &*plan.withoutBeginEnd;
    } else if (disableBegin) {
        validationExpression = &*plan.withoutBegin;
    } else {
        validationExpression = &*plan.withoutEnd;
    }

    if (offset > text.size()) {
        return false;
    }

    RegexMatch validation;
    auto flags = std::regex_constants::match_continuous;
    if (offset != 0U) {
        flags |= std::regex_constants::match_prev_avail;
    }
    const auto first = text.begin() + static_cast<std::ptrdiff_t>(offset);
    if (!std::regex_search(first, text.end(), validation, *validationExpression, flags)) {
        return false;
    }
    return static_cast<std::size_t>(validation.length(0)) == length;
}

template <typename Callback>
void forEachRegexMatch(const std::string_view text,
                       const std::regex& expression,
                       Callback&& callback) {
    auto cursor = text.begin();
    const auto end = text.end();

    while (true) {
        RegexMatch match;
        auto flags = std::regex_constants::match_default;
        if (cursor != text.begin()) {
            flags |= std::regex_constants::match_not_bol;
        }

        if (!std::regex_search(cursor, end, match, expression, flags)) {
            break;
        }

        const auto relative = static_cast<std::size_t>(match.position(0));
        const auto base = static_cast<std::size_t>(std::distance(text.begin(), cursor));
        const auto offset = base + relative;
        const auto length = static_cast<std::size_t>(match.length(0));

        if (!callback(match, offset, length)) {
            break;
        }

        const auto matchBegin = cursor + match.position(0);
        if (length == 0U) {
            if (matchBegin == end) {
                break;
            }
            cursor = std::next(matchBegin);
        } else {
            cursor = matchBegin + match.length(0);
        }
    }
}

[[nodiscard]] bool regexMatchInRange(const std::size_t offset,
                                     const std::size_t length,
                                     const std::size_t begin,
                                     const std::size_t endExclusive) noexcept {
    if (offset < begin || offset >= endExclusive) {
        return false;
    }
    return length <= endExclusive - offset;
}

[[nodiscard]] FindResult findRegex(const std::string_view text,
                                   const std::string_view pattern,
                                   const std::size_t begin,
                                   const std::size_t endExclusive,
                                   const SearchDirection direction,
                                   const SearchOptions& options) {
    try {
        const RegexPlan plan(pattern, options);
        const auto boundedEnd = std::min(endExclusive, text.size());
        if (begin >= boundedEnd) {
            return {};
        }

        std::optional<SearchMatch> selected;
        forEachRegexMatch(text, plan.expression, [&](const RegexMatch&,
                                                     const std::size_t offset,
                                                     const std::size_t length) {
            if (!hasPortableAbsoluteAnchors(text, plan, offset, length)) {
                return true;
            }
            if (!regexMatchInRange(offset, length, begin, boundedEnd)) {
                return offset < boundedEnd;
            }
            if (options.wholeWord && !hasWholeWordBoundaries(text, offset, length)) {
                return true;
            }

            selected = SearchMatch{offset, length};
            return direction != SearchDirection::Forward;
        });
        return {selected, {}};
    } catch (const std::regex_error&) {
        return {{}, std::make_error_code(std::errc::invalid_argument)};
    }
}

[[nodiscard]] FindResult findInRange(const std::string_view text,
                                     const std::string_view pattern,
                                     const std::size_t begin,
                                     const std::size_t endExclusive,
                                     const SearchDirection direction,
                                     const SearchOptions& options) {
    if (options.kind == SearchKind::RegularExpression) {
        return findRegex(text, pattern, begin, endExclusive, direction, options);
    }

    const LiteralPlan plan(pattern, options.caseSensitive);
    const auto matchOffset = direction == SearchDirection::Forward
                                 ? findLiteralForward(text, plan, begin, endExclusive,
                                                      options.wholeWord)
                                 : findLiteralBackward(text, plan, begin, endExclusive,
                                                       options.wholeWord);
    if (!matchOffset) {
        return {};
    }
    return {{SearchMatch{*matchOffset, pattern.size()}}, {}};
}

[[nodiscard]] FindAllResult findAllLiteral(const std::string_view text,
                                           const std::string_view pattern,
                                           const SearchOptions& options) {
    FindAllResult result;
    const LiteralPlan plan(pattern, options.caseSensitive);
    std::size_t cursor = 0U;

    while (cursor < text.size()) {
        const auto found = findLiteralForward(text, plan, cursor, text.size(), options.wholeWord);
        if (!found) {
            break;
        }
        if (result.matches.size() >= options.maxResults) {
            result.truncated = true;
            break;
        }

        result.matches.push_back(SearchMatch{*found, pattern.size()});
        const auto advance = std::max<std::size_t>(pattern.size(), 1U);
        if (*found > std::numeric_limits<std::size_t>::max() - advance) {
            break;
        }
        cursor = *found + advance;
    }
    return result;
}

[[nodiscard]] FindAllResult findAllRegex(const std::string_view text,
                                         const std::string_view pattern,
                                         const SearchOptions& options) {
    FindAllResult result;
    try {
        const RegexPlan plan(pattern, options);
        forEachRegexMatch(text, plan.expression, [&](const RegexMatch&,
                                                     const std::size_t offset,
                                                     const std::size_t length) {
            if (!hasPortableAbsoluteAnchors(text, plan, offset, length)) {
                return true;
            }
            if (options.wholeWord && !hasWholeWordBoundaries(text, offset, length)) {
                return true;
            }
            if (result.matches.size() >= options.maxResults) {
                result.truncated = true;
                return false;
            }
            result.matches.push_back(SearchMatch{offset, length});
            return true;
        });
        return result;
    } catch (const std::regex_error&) {
        result.error = std::make_error_code(std::errc::invalid_argument);
        return result;
    }
}

[[nodiscard]] ReplaceResult replaceLiteralAll(const std::string_view text,
                                              const std::string_view pattern,
                                              const std::string_view replacement,
                                              const SearchOptions& options) {
    const auto found = findAllLiteral(text, pattern, options);
    if (!found) {
        return {{}, 0U, found.error, false};
    }
    if (found.matches.empty()) {
        return {std::string(text), 0U, {}, found.truncated};
    }

    std::string output;
    output.reserve(text.size());
    std::size_t cursor = 0U;
    for (const auto& match : found.matches) {
        const auto offset = static_cast<std::size_t>(match.offset);
        const auto length = static_cast<std::size_t>(match.length);
        output.append(text.substr(cursor, offset - cursor));
        output.append(replacement);
        cursor = offset + length;
    }
    output.append(text.substr(cursor));
    return {std::move(output), found.matches.size(), {}, found.truncated};
}

[[nodiscard]] ReplaceResult replaceRegexAll(const std::string_view text,
                                            const std::string_view pattern,
                                            const std::string_view replacement,
                                            const SearchOptions& options) {
    try {
        const RegexPlan plan(pattern, options);
        std::string output;
        output.reserve(text.size());
        std::size_t copiedUntil = 0U;
        std::size_t replacements = 0U;
        bool truncated = false;

        forEachRegexMatch(text, plan.expression, [&](const RegexMatch& match,
                                                     const std::size_t offset,
                                                     const std::size_t length) {
            if (!hasPortableAbsoluteAnchors(text, plan, offset, length)) {
                return true;
            }
            if (options.wholeWord && !hasWholeWordBoundaries(text, offset, length)) {
                return true;
            }
            if (replacements >= options.maxResults) {
                truncated = true;
                return false;
            }

            output.append(text.substr(copiedUntil, offset - copiedUntil));
            output.append(match.format(std::string(replacement)));
            copiedUntil = offset + length;
            ++replacements;
            return true;
        });

        output.append(text.substr(copiedUntil));
        return {std::move(output), replacements, {}, truncated};
    } catch (const std::regex_error&) {
        return {{}, 0U, std::make_error_code(std::errc::invalid_argument), false};
    }
}

}

FindResult TextSearch::find(const std::string_view text,
                            const std::string_view pattern,
                            const std::size_t startOffset,
                            const SearchDirection direction,
                            const SearchOptions& options) {
    if (pattern.empty()) {
        return {{}, std::make_error_code(std::errc::invalid_argument)};
    }

    const auto start = std::min(startOffset, text.size());
    if (direction == SearchDirection::Forward) {
        auto result = findInRange(text, pattern, start, text.size(), direction, options);
        if (!result || result.match || !options.wrapAround || start == 0U) {
            return result;
        }
        return findInRange(text, pattern, 0U, start, direction, options);
    }

    auto result = findInRange(text, pattern, 0U, start, direction, options);
    if (!result || result.match || !options.wrapAround || start == text.size()) {
        return result;
    }
    return findInRange(text, pattern, start, text.size(), direction, options);
}

FindResult TextSearch::findNext(const std::string_view text,
                                const std::string_view pattern,
                                const std::size_t startOffset,
                                const SearchOptions& options) {
    return find(text, pattern, startOffset, SearchDirection::Forward, options);
}

FindResult TextSearch::findPrevious(const std::string_view text,
                                    const std::string_view pattern,
                                    const std::size_t startOffset,
                                    const SearchOptions& options) {
    return find(text, pattern, startOffset, SearchDirection::Backward, options);
}

FindAllResult TextSearch::findAll(const std::string_view text,
                                  const std::string_view pattern,
                                  const SearchOptions& options) {
    if (pattern.empty()) {
        return {{}, std::make_error_code(std::errc::invalid_argument), false};
    }
    return options.kind == SearchKind::RegularExpression ? findAllRegex(text, pattern, options)
                                                          : findAllLiteral(text, pattern, options);
}

ReplaceMatchResult TextSearch::replaceMatch(const std::string_view text,
                                               const std::string_view pattern,
                                               const std::string_view replacement,
                                               const SearchMatch match,
                                               const SearchOptions& options) {
    ReplaceMatchResult result;
    if (pattern.empty() || match.offset > static_cast<std::uint64_t>(text.size()) ||
        match.length > static_cast<std::uint64_t>(text.size()) - match.offset) {
        result.error = std::make_error_code(std::errc::invalid_argument);
        return result;
    }

    const auto offset = static_cast<std::size_t>(match.offset);
    const auto length = static_cast<std::size_t>(match.length);
    std::string replacementText;

    if (options.kind == SearchKind::Literal) {
        const LiteralPlan plan(pattern, options.caseSensitive);
        const auto literalMatch = findLiteralForward(
            text, plan, offset, offset + length, options.wholeWord);
        if (length != pattern.size() || !literalMatch || *literalMatch != offset) {
            result.error = std::make_error_code(std::errc::invalid_argument);
            return result;
        }
        replacementText.assign(replacement);
    } else {
        try {
            const RegexPlan plan(pattern, options);
            bool found = false;
            forEachRegexMatch(text, plan.expression, [&](const RegexMatch& regexMatch,
                                                         const std::size_t candidateOffset,
                                                         const std::size_t candidateLength) {
                if (candidateOffset > offset) {
                    return false;
                }
                if (candidateOffset != offset || candidateLength != length ||
                    !hasPortableAbsoluteAnchors(text, plan, candidateOffset, candidateLength) ||
                    (options.wholeWord &&
                     !hasWholeWordBoundaries(text, candidateOffset, candidateLength))) {
                    return true;
                }
                replacementText = regexMatch.format(std::string(replacement));
                found = true;
                return false;
            });
            if (!found) {
                result.error = std::make_error_code(std::errc::invalid_argument);
                return result;
            }
        } catch (const std::regex_error&) {
            result.error = std::make_error_code(std::errc::invalid_argument);
            return result;
        }
    }

    result.text.reserve(text.size() - length + replacementText.size());
    result.text.append(text.substr(0U, offset));
    result.text.append(replacementText);
    result.text.append(text.substr(offset + length));
    result.replacementLength = static_cast<std::uint64_t>(replacementText.size());
    result.replaced = true;
    return result;
}

ReplaceResult TextSearch::replaceAll(const std::string_view text,
                                     const std::string_view pattern,
                                     const std::string_view replacement,
                                     const SearchOptions& options) {
    if (pattern.empty()) {
        return {{}, 0U, std::make_error_code(std::errc::invalid_argument), false};
    }
    return options.kind == SearchKind::RegularExpression
               ? replaceRegexAll(text, pattern, replacement, options)
               : replaceLiteralAll(text, pattern, replacement, options);
}

}
