#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace nff::encoding {

enum class Encoding : std::uint8_t {
    Ascii,
    Utf8,
    Utf16LE,
    Utf16BE,
    Utf32LE,
    Utf32BE,
    Windows1252,
    Windows1254,
    Unknown8Bit,
    Binary
};

struct DetectionResult {
    Encoding encoding{Encoding::Utf8};
    bool hasBom{false};
    bool validUtf8{true};
    bool binaryLike{false};
};

class EncodingDetector final {
public:
    [[nodiscard]] static DetectionResult detect(std::span<const std::byte> bytes) noexcept;
    [[nodiscard]] static std::string_view name(Encoding encoding) noexcept;
};

}
