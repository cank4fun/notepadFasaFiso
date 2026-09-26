#include "notepadFasaFiso/encoding/TextCodec.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>

namespace nff::encoding {
namespace {

[[nodiscard]] constexpr unsigned char value(const std::byte byte) noexcept {
    return std::to_integer<unsigned char>(byte);
}

[[nodiscard]] std::error_code invalidSequence() noexcept {
    return std::make_error_code(std::errc::illegal_byte_sequence);
}

[[nodiscard]] std::error_code unsupported() noexcept {
    return std::make_error_code(std::errc::operation_not_supported);
}

void appendUtf8(std::string& output, const std::uint32_t codePoint) {
    if (codePoint <= 0x7FU) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else if (codePoint <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    }
}

struct Utf8Step {
    std::uint32_t codePoint{0};
    std::size_t width{0};
    bool truncated{false};
    bool valid{false};
};

[[nodiscard]] Utf8Step decodeUtf8At(const std::string_view input,
                                    const std::size_t index) noexcept {
    const auto lead = static_cast<unsigned char>(input[index]);
    if (lead <= 0x7FU) {
        return {lead, 1, false, true};
    }

    std::size_t continuationCount = 0;
    std::uint32_t codePoint = 0;
    std::uint32_t minimum = 0;

    if ((lead & 0xE0U) == 0xC0U) {
        continuationCount = 1;
        codePoint = lead & 0x1FU;
        minimum = 0x80U;
    } else if ((lead & 0xF0U) == 0xE0U) {
        continuationCount = 2;
        codePoint = lead & 0x0FU;
        minimum = 0x800U;
    } else if ((lead & 0xF8U) == 0xF0U) {
        continuationCount = 3;
        codePoint = lead & 0x07U;
        minimum = 0x10000U;
    } else {
        return {};
    }

    if (index + continuationCount >= input.size()) {
        return {0, 0, true, false};
    }

    for (std::size_t offset = 1; offset <= continuationCount; ++offset) {
        const auto continuation = static_cast<unsigned char>(input[index + offset]);
        if ((continuation & 0xC0U) != 0x80U) {
            return {};
        }
        codePoint = (codePoint << 6U) | (continuation & 0x3FU);
    }

    if (codePoint < minimum || codePoint > 0x10FFFFU ||
        (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) {
        return {};
    }

    return {codePoint, continuationCount + 1, false, true};
}

[[nodiscard]] bool asciiOnly(const std::string_view input) noexcept {
    constexpr std::uint64_t highBits = 0x8080808080808080ULL;
    std::size_t index = 0U;
    while (index + sizeof(std::uint64_t) <= input.size()) {
        std::uint64_t word = 0U;
        std::memcpy(&word, input.data() + index, sizeof(word));
        if ((word & highBits) != 0U) {
            return false;
        }
        index += sizeof(std::uint64_t);
    }
    while (index < input.size()) {
        if ((static_cast<unsigned char>(input[index]) & 0x80U) != 0U) {
            return false;
        }
        ++index;
    }
    return true;
}

struct Utf8Validation final {
    bool valid{false};
    bool truncated{false};
    std::size_t validBytes{0U};
};

[[nodiscard]] Utf8Validation validateUtf8(const std::string_view input) noexcept {
    constexpr std::uint64_t highBits = 0x8080808080808080ULL;
    std::size_t index = 0U;
    while (index < input.size()) {
        while (index + sizeof(std::uint64_t) <= input.size()) {
            std::uint64_t word = 0U;
            std::memcpy(&word, input.data() + index, sizeof(word));
            if ((word & highBits) != 0U) {
                break;
            }
            index += sizeof(std::uint64_t);
        }
        if (index == input.size()) {
            return {true, false, index};
        }
        if ((static_cast<unsigned char>(input[index]) & 0x80U) == 0U) {
            ++index;
            continue;
        }
        const auto step = decodeUtf8At(input, index);
        if (!step.valid) {
            return {false, step.truncated, index};
        }
        index += step.width;
    }
    return {true, false, index};
}

[[nodiscard]] std::uint16_t read16(const std::span<const std::byte> bytes,
                                   const std::size_t offset,
                                   const bool littleEndian) noexcept {
    const auto first = static_cast<std::uint16_t>(value(bytes[offset]));
    const auto second = static_cast<std::uint16_t>(value(bytes[offset + 1]));
    return littleEndian ? static_cast<std::uint16_t>(first | (second << 8U))
                        : static_cast<std::uint16_t>((first << 8U) | second);
}

[[nodiscard]] std::uint32_t read32(const std::span<const std::byte> bytes,
                                   const std::size_t offset,
                                   const bool littleEndian) noexcept {
    const auto b0 = static_cast<std::uint32_t>(value(bytes[offset]));
    const auto b1 = static_cast<std::uint32_t>(value(bytes[offset + 1]));
    const auto b2 = static_cast<std::uint32_t>(value(bytes[offset + 2]));
    const auto b3 = static_cast<std::uint32_t>(value(bytes[offset + 3]));

    if (littleEndian) {
        return b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U);
    }
    return (b0 << 24U) | (b1 << 16U) | (b2 << 8U) | b3;
}

void append16(std::vector<std::byte>& output, const std::uint16_t value16,
              const bool littleEndian) {
    const auto low = static_cast<unsigned char>(value16 & 0xFFU);
    const auto high = static_cast<unsigned char>((value16 >> 8U) & 0xFFU);
    if (littleEndian) {
        output.push_back(static_cast<std::byte>(low));
        output.push_back(static_cast<std::byte>(high));
    } else {
        output.push_back(static_cast<std::byte>(high));
        output.push_back(static_cast<std::byte>(low));
    }
}

void append32(std::vector<std::byte>& output, const std::uint32_t value32,
              const bool littleEndian) {
    const auto b0 = static_cast<unsigned char>(value32 & 0xFFU);
    const auto b1 = static_cast<unsigned char>((value32 >> 8U) & 0xFFU);
    const auto b2 = static_cast<unsigned char>((value32 >> 16U) & 0xFFU);
    const auto b3 = static_cast<unsigned char>((value32 >> 24U) & 0xFFU);
    if (littleEndian) {
        output.insert(output.end(), {static_cast<std::byte>(b0), static_cast<std::byte>(b1),
                                     static_cast<std::byte>(b2), static_cast<std::byte>(b3)});
    } else {
        output.insert(output.end(), {static_cast<std::byte>(b3), static_cast<std::byte>(b2),
                                     static_cast<std::byte>(b1), static_cast<std::byte>(b0)});
    }
}

constexpr std::array<std::uint32_t, 32> cp1252High{
    0x20ACU, 0x0081U, 0x201AU, 0x0192U, 0x201EU, 0x2026U, 0x2020U, 0x2021U,
    0x02C6U, 0x2030U, 0x0160U, 0x2039U, 0x0152U, 0x008DU, 0x017DU, 0x008FU,
    0x0090U, 0x2018U, 0x2019U, 0x201CU, 0x201DU, 0x2022U, 0x2013U, 0x2014U,
    0x02DCU, 0x2122U, 0x0161U, 0x203AU, 0x0153U, 0x009DU, 0x017EU, 0x0178U};

[[nodiscard]] std::uint32_t decodeWindowsByte(const unsigned char byte,
                                              const Encoding encoding) noexcept {
    if (byte < 0x80U || byte >= 0xA0U) {
        if (encoding == Encoding::Windows1254) {
            switch (byte) {
            case 0xD0U:
                return 0x011EU;
            case 0xDDU:
                return 0x0130U;
            case 0xDEU:
                return 0x015EU;
            case 0xF0U:
                return 0x011FU;
            case 0xFDU:
                return 0x0131U;
            case 0xFEU:
                return 0x015FU;
            default:
                break;
            }
        }
        return byte;
    }
    return cp1252High[byte - 0x80U];
}

[[nodiscard]] bool encodeWindowsCodePoint(const std::uint32_t codePoint,
                                          const Encoding encoding,
                                          unsigned char& output) noexcept {
    if (codePoint < 0x80U) {
        output = static_cast<unsigned char>(codePoint);
        return true;
    }

    if (encoding == Encoding::Windows1254) {
        switch (codePoint) {
        case 0x011EU:
            output = 0xD0U;
            return true;
        case 0x0130U:
            output = 0xDDU;
            return true;
        case 0x015EU:
            output = 0xDEU;
            return true;
        case 0x011FU:
            output = 0xF0U;
            return true;
        case 0x0131U:
            output = 0xFDU;
            return true;
        case 0x015FU:
            output = 0xFEU;
            return true;
        default:
            break;
        }
    }

    if (codePoint >= 0xA0U && codePoint <= 0xFFU) {
        const auto byte = static_cast<unsigned char>(codePoint);
        if (encoding == Encoding::Windows1254 &&
            (byte == 0xD0U || byte == 0xDDU || byte == 0xDEU || byte == 0xF0U ||
             byte == 0xFDU || byte == 0xFEU)) {
            return false;
        }
        output = byte;
        return true;
    }

    for (std::size_t index = 0; index < cp1252High.size(); ++index) {
        if (cp1252High[index] == codePoint) {
            output = static_cast<unsigned char>(0x80U + index);
            return true;
        }
    }

    return false;
}

[[nodiscard]] std::size_t bomSize(const Encoding encoding) noexcept {
    switch (encoding) {
    case Encoding::Utf8:
        return 3;
    case Encoding::Utf16LE:
    case Encoding::Utf16BE:
        return 2;
    case Encoding::Utf32LE:
    case Encoding::Utf32BE:
        return 4;
    default:
        return 0;
    }
}

[[nodiscard]] bool hasExpectedBom(const std::span<const std::byte> bytes,
                                  const Encoding encoding) noexcept {
    const auto matches = [bytes](const std::initializer_list<unsigned char> expected) {
        if (bytes.size() < expected.size()) {
            return false;
        }
        std::size_t index = 0;
        for (const auto current : expected) {
            if (value(bytes[index]) != current) {
                return false;
            }
            ++index;
        }
        return true;
    };

    switch (encoding) {
    case Encoding::Utf8:
        return matches({0xEFU, 0xBBU, 0xBFU});
    case Encoding::Utf16LE:
        return matches({0xFFU, 0xFEU});
    case Encoding::Utf16BE:
        return matches({0xFEU, 0xFFU});
    case Encoding::Utf32LE:
        return matches({0xFFU, 0xFEU, 0x00U, 0x00U});
    case Encoding::Utf32BE:
        return matches({0x00U, 0x00U, 0xFEU, 0xFFU});
    default:
        return false;
    }
}

[[nodiscard]] DecodeResult decodeUtf8Bytes(std::span<const std::byte> bytes,
                                           const bool hasBom,
                                           const DecodeOptions& options) {
    if (hasBom && !hasExpectedBom(bytes, Encoding::Utf8)) {
        return {{}, invalidSequence()};
    }
    const auto skip = hasBom ? bomSize(Encoding::Utf8) : 0U;

    std::string input;
    if (bytes.size() > skip) {
        input.assign(reinterpret_cast<const char*>(bytes.data() + skip), bytes.size() - skip);
    }
    const auto validation = validateUtf8(input);
    if (!validation.valid) {
        if (validation.truncated && options.allowTruncatedTail) {
            input.resize(validation.validBytes);
        } else {
            return {{}, invalidSequence()};
        }
    }
    return {std::move(input), {}};
}

[[nodiscard]] DecodeResult decodeUtf16(std::span<const std::byte> bytes,
                                       const bool littleEndian,
                                       const bool hasBom,
                                       const DecodeOptions& options) {
    const auto encoding = littleEndian ? Encoding::Utf16LE : Encoding::Utf16BE;
    if (hasBom && !hasExpectedBom(bytes, encoding)) {
        return {{}, invalidSequence()};
    }
    const std::size_t skip = hasBom ? 2U : 0U;

    std::string output;
    output.reserve(bytes.size());
    std::size_t offset = skip;
    while (offset < bytes.size()) {
        if (offset + 1 >= bytes.size()) {
            if (options.allowTruncatedTail) {
                break;
            }
            return {{}, invalidSequence()};
        }

        const auto unit = read16(bytes, offset, littleEndian);
        offset += 2;
        std::uint32_t codePoint = unit;

        if (unit >= 0xD800U && unit <= 0xDBFFU) {
            if (offset + 1 >= bytes.size()) {
                if (options.allowTruncatedTail) {
                    break;
                }
                return {{}, invalidSequence()};
            }
            const auto low = read16(bytes, offset, littleEndian);
            if (low < 0xDC00U || low > 0xDFFFU) {
                return {{}, invalidSequence()};
            }
            offset += 2;
            codePoint = 0x10000U +
                        ((static_cast<std::uint32_t>(unit) - 0xD800U) << 10U) +
                        (static_cast<std::uint32_t>(low) - 0xDC00U);
        } else if (unit >= 0xDC00U && unit <= 0xDFFFU) {
            return {{}, invalidSequence()};
        }

        appendUtf8(output, codePoint);
    }

    return {std::move(output), {}};
}

[[nodiscard]] DecodeResult decodeUtf32(std::span<const std::byte> bytes,
                                       const bool littleEndian,
                                       const bool hasBom,
                                       const DecodeOptions& options) {
    const auto encoding = littleEndian ? Encoding::Utf32LE : Encoding::Utf32BE;
    if (hasBom && !hasExpectedBom(bytes, encoding)) {
        return {{}, invalidSequence()};
    }
    const std::size_t skip = hasBom ? 4U : 0U;

    std::string output;
    output.reserve(bytes.size());
    for (std::size_t offset = skip; offset < bytes.size(); offset += 4) {
        if (offset + 3 >= bytes.size()) {
            if (options.allowTruncatedTail) {
                break;
            }
            return {{}, invalidSequence()};
        }

        const auto codePoint = read32(bytes, offset, littleEndian);
        if (codePoint > 0x10FFFFU || (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) {
            return {{}, invalidSequence()};
        }
        appendUtf8(output, codePoint);
    }

    return {std::move(output), {}};
}

}

bool TextCodec::isValidUtf8(const std::string_view utf8) noexcept {
    return validateUtf8(utf8).valid;
}

DecodeResult TextCodec::decode(const std::span<const std::byte> bytes,
                               const Encoding encoding,
                               const bool hasBom,
                               const DecodeOptions& options) {
    switch (encoding) {
    case Encoding::Ascii: {
        std::string output;
        if (!bytes.empty()) {
            output.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
        if (!asciiOnly(output)) {
            return {{}, invalidSequence()};
        }
        return {std::move(output), {}};
    }
    case Encoding::Utf8:
        return decodeUtf8Bytes(bytes, hasBom, options);
    case Encoding::Utf16LE:
        return decodeUtf16(bytes, true, hasBom, options);
    case Encoding::Utf16BE:
        return decodeUtf16(bytes, false, hasBom, options);
    case Encoding::Utf32LE:
        return decodeUtf32(bytes, true, hasBom, options);
    case Encoding::Utf32BE:
        return decodeUtf32(bytes, false, hasBom, options);
    case Encoding::Windows1252:
    case Encoding::Windows1254: {
        std::string output;
        output.reserve(bytes.size() * 2U);
        for (const auto byte : bytes) {
            appendUtf8(output, decodeWindowsByte(value(byte), encoding));
        }
        return {std::move(output), {}};
    }
    case Encoding::Unknown8Bit:
    case Encoding::Binary:
        return {{}, unsupported()};
    }
    return {{}, unsupported()};
}

DecodeResult TextCodec::decode(const std::span<const std::byte> bytes,
                               const DetectionResult& detection,
                               const DecodeOptions& options) {
    return decode(bytes, detection.encoding, detection.hasBom, options);
}

EncodeResult TextCodec::encode(const std::string_view utf8,
                               const Encoding encoding,
                               const bool withBom) {
    if (encoding == Encoding::Unknown8Bit || encoding == Encoding::Binary) {
        return {{}, unsupported()};
    }

    if (encoding == Encoding::Utf8) {
        const auto validation = validateUtf8(utf8);
        if (!validation.valid) {
            return {{}, invalidSequence()};
        }
        const auto prefix = withBom ? std::size_t{3U} : std::size_t{0U};
        std::vector<std::byte> output(prefix + utf8.size());
        if (withBom) {
            output[0] = std::byte{0xEF};
            output[1] = std::byte{0xBB};
            output[2] = std::byte{0xBF};
        }
        if (!utf8.empty()) {
            std::memcpy(output.data() + prefix, utf8.data(), utf8.size());
        }
        return {std::move(output), {}};
    }

    std::vector<std::byte> output;
    output.reserve(utf8.size() + 4U);

    if (withBom) {
        switch (encoding) {
        case Encoding::Utf8:
            output.insert(output.end(), {std::byte{0xEF}, std::byte{0xBB}, std::byte{0xBF}});
            break;
        case Encoding::Utf16LE:
            output.insert(output.end(), {std::byte{0xFF}, std::byte{0xFE}});
            break;
        case Encoding::Utf16BE:
            output.insert(output.end(), {std::byte{0xFE}, std::byte{0xFF}});
            break;
        case Encoding::Utf32LE:
            output.insert(output.end(),
                          {std::byte{0xFF}, std::byte{0xFE}, std::byte{0x00}, std::byte{0x00}});
            break;
        case Encoding::Utf32BE:
            output.insert(output.end(),
                          {std::byte{0x00}, std::byte{0x00}, std::byte{0xFE}, std::byte{0xFF}});
            break;
        default:
            break;
        }
    }

    std::size_t index = 0;
    while (index < utf8.size()) {
        const auto step = decodeUtf8At(utf8, index);
        if (!step.valid) {
            return {{}, invalidSequence()};
        }
        index += step.width;

        switch (encoding) {
        case Encoding::Ascii:
            if (step.codePoint > 0x7FU) {
                return {{}, invalidSequence()};
            }
            output.push_back(static_cast<std::byte>(step.codePoint));
            break;
        case Encoding::Utf8:
            for (std::size_t offset = index - step.width; offset < index; ++offset) {
                output.push_back(static_cast<std::byte>(
                    static_cast<unsigned char>(utf8[offset])));
            }
            break;
        case Encoding::Utf16LE:
        case Encoding::Utf16BE: {
            const bool littleEndian = encoding == Encoding::Utf16LE;
            if (step.codePoint <= 0xFFFFU) {
                append16(output, static_cast<std::uint16_t>(step.codePoint), littleEndian);
            } else {
                const auto adjusted = step.codePoint - 0x10000U;
                const auto high = static_cast<std::uint16_t>(0xD800U + (adjusted >> 10U));
                const auto low = static_cast<std::uint16_t>(0xDC00U + (adjusted & 0x3FFU));
                append16(output, high, littleEndian);
                append16(output, low, littleEndian);
            }
            break;
        }
        case Encoding::Utf32LE:
        case Encoding::Utf32BE:
            append32(output, step.codePoint, encoding == Encoding::Utf32LE);
            break;
        case Encoding::Windows1252:
        case Encoding::Windows1254: {
            unsigned char byte = 0;
            if (!encodeWindowsCodePoint(step.codePoint, encoding, byte)) {
                return {{}, invalidSequence()};
            }
            output.push_back(static_cast<std::byte>(byte));
            break;
        }
        case Encoding::Unknown8Bit:
        case Encoding::Binary:
            return {{}, unsupported()};
        }
    }

    return {std::move(output), {}};
}

}
