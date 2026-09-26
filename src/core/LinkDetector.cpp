#include "notepadFasaFiso/core/LinkDetector.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace nff::core {
namespace {

[[nodiscard]] char asciiLower(const char value) noexcept {
    const auto byte = static_cast<unsigned char>(value);
    return static_cast<char>(std::tolower(byte));
}

[[nodiscard]] bool startsWithInsensitive(const std::string_view text,
                                         const std::size_t offset,
                                         const std::string_view prefix) noexcept {
    if (offset > text.size() || prefix.size() > text.size() - offset) {
        return false;
    }
    for (std::size_t index = 0U; index < prefix.size(); ++index) {
        if (asciiLower(text[offset + index]) != asciiLower(prefix[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool boundaryBefore(const std::string_view text, const std::size_t offset) noexcept {
    if (offset == 0U) {
        return true;
    }
    const auto ch = static_cast<unsigned char>(text[offset - 1U]);
    return std::isalnum(ch) == 0 && ch != '_' && ch != '-';
}

[[nodiscard]] bool terminator(const char ch) noexcept {
    const auto byte = static_cast<unsigned char>(ch);
    return std::isspace(byte) != 0 || ch == '<' || ch == '>' || ch == '"' || ch == '\'' ||
           ch == '`';
}

[[nodiscard]] bool trailingPunctuation(const char ch) noexcept {
    return ch == '.' || ch == ',' || ch == ';' || ch == ':' || ch == '!' || ch == '?' ||
           ch == ')' || ch == ']' || ch == '}';
}

[[nodiscard]] std::size_t scanTokenEnd(const std::string_view text, const std::size_t begin) noexcept {
    auto end = begin;
    while (end < text.size() && !terminator(text[end])) {
        ++end;
    }
    while (end > begin && trailingPunctuation(text[end - 1U])) {
        --end;
    }
    return end;
}

[[nodiscard]] bool emailLocalChar(const char ch) noexcept {
    const auto byte = static_cast<unsigned char>(ch);
    return std::isalnum(byte) != 0 || ch == '.' || ch == '_' || ch == '%' || ch == '+' || ch == '-';
}

[[nodiscard]] bool emailDomainChar(const char ch) noexcept {
    const auto byte = static_cast<unsigned char>(ch);
    return std::isalnum(byte) != 0 || ch == '.' || ch == '-';
}

[[nodiscard]] bool plausibleEmail(const std::string_view token) noexcept {
    const auto at = token.find('@');
    if (at == std::string_view::npos || at == 0U || at + 3U > token.size() ||
        token.find('@', at + 1U) != std::string_view::npos) {
        return false;
    }
    if (token.find('.', at + 2U) == std::string_view::npos) {
        return false;
    }
    return std::all_of(token.begin(), token.begin() + static_cast<std::ptrdiff_t>(at), emailLocalChar) &&
           std::all_of(token.begin() + static_cast<std::ptrdiff_t>(at + 1U), token.end(), emailDomainChar);
}

[[nodiscard]] std::string normalizedTarget(const LinkKind kind, const std::string_view text) {
    if (kind == LinkKind::Www) {
        return "https://" + std::string(text);
    }
    if (kind == LinkKind::Email) {
        return "mailto:" + std::string(text);
    }
    return std::string(text);
}

}

std::vector<LinkSpan> LinkDetector::detect(const std::string_view utf8Text,
                                           const std::size_t maxResults) {
    std::vector<LinkSpan> results;
    if (utf8Text.empty() || maxResults == 0U) {
        return results;
    }
    results.reserve(std::min<std::size_t>(maxResults, 64U));

    std::size_t offset = 0U;
    while (offset < utf8Text.size() && results.size() < maxResults) {
        if (!boundaryBefore(utf8Text, offset)) {
            ++offset;
            continue;
        }

        LinkKind kind{};
        std::size_t prefixLength = 0U;
        bool matched = true;
        if (startsWithInsensitive(utf8Text, offset, "https://")) {
            kind = LinkKind::Https;
            prefixLength = 8U;
        } else if (startsWithInsensitive(utf8Text, offset, "http://")) {
            kind = LinkKind::Http;
            prefixLength = 7U;
        } else if (startsWithInsensitive(utf8Text, offset, "www.")) {
            kind = LinkKind::Www;
            prefixLength = 4U;
        } else if (startsWithInsensitive(utf8Text, offset, "mailto:")) {
            kind = LinkKind::Mailto;
            prefixLength = 7U;
        } else {
            matched = false;
        }

        if (matched) {
            const auto end = scanTokenEnd(utf8Text, offset + prefixLength);
            if (end > offset + prefixLength) {
                const auto token = utf8Text.substr(offset, end - offset);
                results.push_back({offset, token.size(), kind, normalizedTarget(kind, token)});
                offset = end;
                continue;
            }
        }

        if (emailLocalChar(utf8Text[offset])) {
            auto begin = offset;
            auto end = begin;
            while (end < utf8Text.size() && !terminator(utf8Text[end])) {
                ++end;
            }
            while (end > begin && trailingPunctuation(utf8Text[end - 1U])) {
                --end;
            }
            const auto token = utf8Text.substr(begin, end - begin);
            if (plausibleEmail(token)) {
                results.push_back({begin, token.size(), LinkKind::Email,
                                   normalizedTarget(LinkKind::Email, token)});
                offset = end;
                continue;
            }
        }

        ++offset;
    }
    return results;
}

}
