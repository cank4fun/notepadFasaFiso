#include "notepadFasaFiso/core/TextTransform.hpp"

namespace nff::core {
namespace {

[[nodiscard]] constexpr std::string_view separator(const LineEndingPolicy policy) noexcept {
    switch (policy) {
    case LineEndingPolicy::LF:
        return "\n";
    case LineEndingPolicy::CRLF:
        return "\r\n";
    case LineEndingPolicy::CR:
        return "\r";
    case LineEndingPolicy::Preserve:
        break;
    }
    return {};
}

}

std::string TextTransform::normalizeLineEndings(const std::string_view text,
                                                const LineEndingPolicy policy) {
    if (policy == LineEndingPolicy::Preserve) {
        return std::string{text};
    }

    const auto replacement = separator(policy);
    std::string output;
    output.reserve(text.size());

    for (std::size_t index = 0; index < text.size(); ++index) {
        const char value = text[index];
        if (value == '\r') {
            if (index + 1 < text.size() && text[index + 1] == '\n') {
                ++index;
            }
            output.append(replacement);
        } else if (value == '\n') {
            output.append(replacement);
        } else {
            output.push_back(value);
        }
    }

    return output;
}

}
