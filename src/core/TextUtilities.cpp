#include "notepadFasaFiso/core/TextUtilities.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nff::core {
namespace {

struct LineRecord final {
    std::string_view content;
    std::string_view terminator;
};

[[nodiscard]] std::vector<LineRecord> splitLines(const std::string_view text) {
    std::vector<LineRecord> lines;
    if (text.empty()) {
        return lines;
    }

    std::size_t start = 0U;
    std::size_t index = 0U;
    while (index < text.size()) {
        if (text[index] != '\r' && text[index] != '\n') {
            ++index;
            continue;
        }

        const auto contentEnd = index;
        if (text[index] == '\r' && index + 1U < text.size() && text[index + 1U] == '\n') {
            index += 2U;
        } else {
            ++index;
        }
        lines.push_back({text.substr(start, contentEnd - start),
                         text.substr(contentEnd, index - contentEnd)});
        start = index;
    }

    if (start < text.size()) {
        lines.push_back({text.substr(start), {}});
    }
    return lines;
}

[[nodiscard]] std::string_view fallbackTerminator(const std::vector<LineRecord>& lines) noexcept {
    std::size_t lf = 0U;
    std::size_t crlf = 0U;
    std::size_t cr = 0U;
    for (const auto& line : lines) {
        if (line.terminator == "\n") {
            ++lf;
        } else if (line.terminator == "\r\n") {
            ++crlf;
        } else if (line.terminator == "\r") {
            ++cr;
        }
    }

    if (crlf >= lf && crlf >= cr && crlf != 0U) {
        return "\r\n";
    }
    if (lf >= cr && lf != 0U) {
        return "\n";
    }
    if (cr != 0U) {
        return "\r";
    }
    return "\n";
}

[[nodiscard]] bool isAsciiWhitespaceOnly(const std::string_view value) noexcept {
    return std::all_of(value.begin(), value.end(), [](const char ch) {
        return ch == ' ' || ch == '\t';
    });
}

[[nodiscard]] char asciiLower(const char value) noexcept {
    const auto byte = static_cast<unsigned char>(value);
    if (byte >= static_cast<unsigned char>('A') && byte <= static_cast<unsigned char>('Z')) {
        return static_cast<char>(byte + (static_cast<unsigned char>('a') -
                                         static_cast<unsigned char>('A')));
    }
    return value;
}

[[nodiscard]] char asciiUpper(const char value) noexcept {
    const auto byte = static_cast<unsigned char>(value);
    if (byte >= static_cast<unsigned char>('a') && byte <= static_cast<unsigned char>('z')) {
        return static_cast<char>(byte - (static_cast<unsigned char>('a') -
                                         static_cast<unsigned char>('A')));
    }
    return value;
}

[[nodiscard]] std::string asciiFold(const std::string_view value) {
    std::string folded;
    folded.reserve(value.size());
    for (const char ch : value) {
        folded.push_back(asciiLower(ch));
    }
    return folded;
}

[[nodiscard]] int compareAscii(const std::string_view left,
                               const std::string_view right,
                               const bool caseSensitive) noexcept {
    const auto common = std::min(left.size(), right.size());
    for (std::size_t index = 0U; index < common; ++index) {
        const auto leftChar = caseSensitive ? left[index] : asciiLower(left[index]);
        const auto rightChar = caseSensitive ? right[index] : asciiLower(right[index]);
        const auto leftByte = static_cast<unsigned char>(leftChar);
        const auto rightByte = static_cast<unsigned char>(rightChar);
        if (leftByte < rightByte) {
            return -1;
        }
        if (leftByte > rightByte) {
            return 1;
        }
    }
    if (left.size() < right.size()) {
        return -1;
    }
    if (left.size() > right.size()) {
        return 1;
    }
    return 0;
}

[[nodiscard]] std::string rebuildReordered(const std::vector<LineRecord>& original,
                                           const std::vector<std::size_t>& order) {
    if (order.empty()) {
        return {};
    }

    std::size_t reserveBytes = 0U;
    for (const auto& line : original) {
        reserveBytes += line.content.size() + line.terminator.size();
    }

    std::string output;
    output.reserve(reserveBytes);
    for (std::size_t position = 0U; position < order.size(); ++position) {
        output.append(original[order[position]].content);
        if (position < original.size()) {
            output.append(original[position].terminator);
        }
    }
    return output;
}

[[nodiscard]] std::string rebuildFiltered(const std::vector<LineRecord>& original,
                                          const std::vector<std::size_t>& selected) {
    if (selected.empty()) {
        return {};
    }

    const auto fallback = fallbackTerminator(original);
    const bool originalEndedWithTerminator = !original.empty() && !original.back().terminator.empty();

    std::size_t reserveBytes = 0U;
    for (const auto index : selected) {
        reserveBytes += original[index].content.size() + original[index].terminator.size();
    }

    std::string output;
    output.reserve(reserveBytes);
    for (std::size_t position = 0U; position < selected.size(); ++position) {
        const auto& line = original[selected[position]];
        output.append(line.content);

        const bool isLast = position + 1U == selected.size();
        if (isLast) {
            if (originalEndedWithTerminator) {
                output.append(!line.terminator.empty() ? line.terminator : fallback);
            }
        } else {
            output.append(!line.terminator.empty() ? line.terminator : fallback);
        }
    }
    return output;
}

[[nodiscard]] std::size_t utf8CodePointBytes(const unsigned char lead) noexcept {
    if ((lead & 0x80U) == 0U) {
        return 1U;
    }
    if ((lead & 0xE0U) == 0xC0U) {
        return 2U;
    }
    if ((lead & 0xF0U) == 0xE0U) {
        return 3U;
    }
    if ((lead & 0xF8U) == 0xF0U) {
        return 4U;
    }
    return 1U;
}

}

std::string TextUtilities::trimTrailingWhitespace(const std::string_view text) {
    const auto lines = splitLines(text);
    if (lines.empty()) {
        return std::string{text};
    }

    std::string output;
    output.reserve(text.size());
    for (const auto& line : lines) {
        auto end = line.content.size();
        while (end != 0U && (line.content[end - 1U] == ' ' || line.content[end - 1U] == '\t')) {
            --end;
        }
        output.append(line.content.substr(0U, end));
        output.append(line.terminator);
    }
    return output;
}

std::string TextUtilities::removeEmptyLines(const std::string_view text,
                                            const bool whitespaceOnly) {
    const auto lines = splitLines(text);
    if (lines.empty()) {
        return std::string{text};
    }

    std::vector<std::size_t> selected;
    selected.reserve(lines.size());
    for (std::size_t index = 0U; index < lines.size(); ++index) {
        const bool empty = lines[index].content.empty() ||
                           (whitespaceOnly && isAsciiWhitespaceOnly(lines[index].content));
        if (!empty) {
            selected.push_back(index);
        }
    }
    return rebuildFiltered(lines, selected);
}

std::string TextUtilities::removeDuplicateLines(const std::string_view text,
                                                const DuplicateLineOptions& options) {
    const auto lines = splitLines(text);
    if (lines.empty()) {
        return std::string{text};
    }

    std::vector<std::size_t> selected;
    selected.reserve(lines.size());
    std::unordered_set<std::string> seen;
    seen.reserve(lines.size());

    for (std::size_t index = 0U; index < lines.size(); ++index) {
        const auto content = lines[index].content;
        if (content.empty()) {
            if (options.keepEmptyLines) {
                selected.push_back(index);
            }
            continue;
        }
        auto key = options.caseSensitive ? std::string{content} : asciiFold(content);
        if (seen.insert(std::move(key)).second) {
            selected.push_back(index);
        }
    }
    return rebuildFiltered(lines, selected);
}

std::string TextUtilities::sortLines(const std::string_view text,
                                     const LineSortDirection direction,
                                     const bool caseSensitive) {
    const auto lines = splitLines(text);
    if (lines.size() < 2U) {
        return std::string{text};
    }

    std::vector<std::size_t> order(lines.size());
    for (std::size_t index = 0U; index < order.size(); ++index) {
        order[index] = index;
    }

    std::stable_sort(order.begin(), order.end(), [&](const std::size_t left,
                                                      const std::size_t right) {
        const auto comparison = compareAscii(lines[left].content, lines[right].content,
                                             caseSensitive);
        return direction == LineSortDirection::Ascending ? comparison < 0 : comparison > 0;
    });
    return rebuildReordered(lines, order);
}

std::string TextUtilities::reverseLines(const std::string_view text) {
    const auto lines = splitLines(text);
    if (lines.size() < 2U) {
        return std::string{text};
    }

    std::vector<std::size_t> order(lines.size());
    for (std::size_t index = 0U; index < order.size(); ++index) {
        order[index] = order.size() - index - 1U;
    }
    return rebuildReordered(lines, order);
}

std::string TextUtilities::expandTabs(const std::string_view text, const std::size_t tabWidth) {
    if (tabWidth == 0U) {
        return std::string{text};
    }

    std::string output;
    output.reserve(text.size());
    std::size_t column = 0U;

    for (std::size_t index = 0U; index < text.size();) {
        const char ch = text[index];
        if (ch == '\t') {
            const auto spaces = tabWidth - (column % tabWidth);
            output.append(spaces, ' ');
            column += spaces;
            ++index;
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            output.push_back(ch);
            ++index;
            if (ch == '\r' && index < text.size() && text[index] == '\n') {
                output.push_back('\n');
                ++index;
            }
            column = 0U;
            continue;
        }

        const auto lead = static_cast<unsigned char>(ch);
        const auto requested = utf8CodePointBytes(lead);
        const auto remaining = text.size() - index;
        const auto count = std::min(requested, remaining);
        output.append(text.substr(index, count));
        index += count;
        ++column;
    }
    return output;
}

std::string TextUtilities::compressLeadingSpacesToTabs(const std::string_view text,
                                                        const std::size_t tabWidth) {
    if (tabWidth == 0U) {
        return std::string{text};
    }

    std::string output;
    output.reserve(text.size());
    bool atLineStart = true;
    std::size_t pendingSpaces = 0U;

    const auto flushSpaces = [&]() {
        if (pendingSpaces == 0U) {
            return;
        }
        const auto tabs = pendingSpaces / tabWidth;
        const auto spaces = pendingSpaces % tabWidth;
        output.append(tabs, '\t');
        output.append(spaces, ' ');
        pendingSpaces = 0U;
    };

    for (std::size_t index = 0U; index < text.size(); ++index) {
        const char ch = text[index];
        if (atLineStart && ch == ' ') {
            ++pendingSpaces;
            continue;
        }
        flushSpaces();

        if (ch == '\r' || ch == '\n') {
            output.push_back(ch);
            if (ch == '\r' && index + 1U < text.size() && text[index + 1U] == '\n') {
                output.push_back('\n');
                ++index;
            }
            atLineStart = true;
            continue;
        }

        output.push_back(ch);
        if (atLineStart && ch != '\t') {
            atLineStart = false;
        }
    }
    flushSpaces();
    return output;
}

std::string TextUtilities::asciiToLower(const std::string_view text) {
    std::string output;
    output.reserve(text.size());
    for (const char ch : text) {
        output.push_back(asciiLower(ch));
    }
    return output;
}

std::string TextUtilities::asciiToUpper(const std::string_view text) {
    std::string output;
    output.reserve(text.size());
    for (const char ch : text) {
        output.push_back(asciiUpper(ch));
    }
    return output;
}

}
