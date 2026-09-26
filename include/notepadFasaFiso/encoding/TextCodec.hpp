#pragma once

#include "notepadFasaFiso/encoding/EncodingDetector.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace nff::encoding {

struct DecodeOptions {
    bool allowTruncatedTail{false};
};

struct DecodeResult {
    std::string text;
    std::error_code error{};

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

struct EncodeResult {
    std::vector<std::byte> bytes;
    std::error_code error{};

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

class TextCodec final {
public:
    [[nodiscard]] static bool isValidUtf8(std::string_view utf8) noexcept;
    [[nodiscard]] static DecodeResult decode(std::span<const std::byte> bytes,
                                             Encoding encoding,
                                             bool hasBom = false,
                                             const DecodeOptions& options = {});
    [[nodiscard]] static DecodeResult decode(std::span<const std::byte> bytes,
                                             const DetectionResult& detection,
                                             const DecodeOptions& options = {});
    [[nodiscard]] static EncodeResult encode(std::string_view utf8,
                                             Encoding encoding,
                                             bool withBom = false);
};

}
