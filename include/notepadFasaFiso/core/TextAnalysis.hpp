#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace nff::core {

enum class LineEnding : std::uint8_t {
    Unknown,
    LF,
    CRLF,
    CR,
    Mixed
};

struct TextStatistics {
    LineEnding lineEnding{LineEnding::Unknown};
    std::size_t lineCount{0};
    std::size_t longestLineBytes{0};
    std::size_t lfCount{0};
    std::size_t crlfCount{0};
    std::size_t crCount{0};
    std::size_t longestLineOccurrences{0};
};

class TextAnalysis final {
public:
    [[nodiscard]] static TextStatistics analyzeUtf8(std::string_view text) noexcept;
    [[nodiscard]] static TextStatistics analyzeAsciiCompatible(
        std::span<const std::byte> bytes) noexcept;
};

}
