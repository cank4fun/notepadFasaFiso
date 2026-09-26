#include "notepadFasaFiso/encoding/EncodingDetector.hpp"

#include <algorithm>
#include <array>

namespace nff::encoding {
namespace {

[[nodiscard]] constexpr unsigned char value(const std::byte byte) noexcept {
    return std::to_integer<unsigned char>(byte);
}

[[nodiscard]] bool startsWith(std::span<const std::byte> bytes,
                              std::span<const unsigned char> prefix) noexcept {
    if (bytes.size() < prefix.size()) {
        return false;
    }

    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (value(bytes[index]) != prefix[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool isValidUtf8(std::span<const std::byte> bytes) noexcept {
    std::size_t index = 0;
    while (index < bytes.size()) {
        const auto lead = value(bytes[index]);
        if (lead <= 0x7FU) {
            ++index;
            continue;
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
            return false;
        }

        if (index + continuationCount >= bytes.size()) {
            return false;
        }

        for (std::size_t offset = 1; offset <= continuationCount; ++offset) {
            const auto continuation = value(bytes[index + offset]);
            if ((continuation & 0xC0U) != 0x80U) {
                return false;
            }
            codePoint = (codePoint << 6U) | (continuation & 0x3FU);
        }

        if (codePoint < minimum || codePoint > 0x10FFFFU ||
            (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) {
            return false;
        }

        index += continuationCount + 1;
    }

    return true;
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

[[nodiscard]] bool validUtf16(std::span<const std::byte> bytes,
                              const bool littleEndian) noexcept {
    if (bytes.size() < 2 || (bytes.size() % 2U) != 0U) {
        return false;
    }

    for (std::size_t offset = 0; offset < bytes.size(); offset += 2) {
        const auto unit = read16(bytes, offset, littleEndian);
        if (unit >= 0xD800U && unit <= 0xDBFFU) {
            if (offset + 3 >= bytes.size()) {
                return false;
            }
            const auto low = read16(bytes, offset + 2, littleEndian);
            if (low < 0xDC00U || low > 0xDFFFU) {
                return false;
            }
            offset += 2;
        } else if (unit >= 0xDC00U && unit <= 0xDFFFU) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool validUtf32(std::span<const std::byte> bytes,
                              const bool littleEndian) noexcept {
    if (bytes.size() < 4 || (bytes.size() % 4U) != 0U) {
        return false;
    }

    for (std::size_t offset = 0; offset < bytes.size(); offset += 4) {
        const auto codePoint = read32(bytes, offset, littleEndian);
        if (codePoint > 0x10FFFFU || (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool looksUtf32(std::span<const std::byte> bytes,
                              const bool littleEndian) noexcept {
    if (bytes.size() < 8) {
        return false;
    }

    const auto completeSize = bytes.size() - (bytes.size() % 4U);
    const auto sample = bytes.first(completeSize);
    if (!validUtf32(sample, littleEndian)) {
        return false;
    }

    std::array<std::size_t, 4> zeroCounts{};
    std::array<std::size_t, 4> totals{};
    for (std::size_t index = 0; index < sample.size(); ++index) {
        const auto lane = index % 4U;
        ++totals[lane];
        if (value(sample[index]) == 0U) {
            ++zeroCounts[lane];
        }
    }

    const auto ratioAtLeast = [&](const std::size_t lane, const std::size_t percent) {
        return zeroCounts[lane] * 100U >= totals[lane] * percent;
    };
    const auto ratioAtMost = [&](const std::size_t lane, const std::size_t percent) {
        return zeroCounts[lane] * 100U <= totals[lane] * percent;
    };

    if (littleEndian) {
        return ratioAtMost(0, 20) && ratioAtLeast(1, 70) && ratioAtLeast(2, 85) &&
               ratioAtLeast(3, 85);
    }
    return ratioAtLeast(0, 85) && ratioAtLeast(1, 85) && ratioAtLeast(2, 70) &&
           ratioAtMost(3, 20);
}

[[nodiscard]] bool looksUtf16(std::span<const std::byte> bytes,
                              const bool littleEndian) noexcept {
    if (bytes.size() < 4) {
        return false;
    }

    const auto completeSize = bytes.size() - (bytes.size() % 2U);
    const auto sample = bytes.first(completeSize);
    if (!validUtf16(sample, littleEndian)) {
        return false;
    }

    std::size_t evenZeros = 0;
    std::size_t oddZeros = 0;
    std::size_t pairs = 0;
    for (std::size_t index = 0; index + 1 < sample.size(); index += 2) {
        ++pairs;
        evenZeros += value(sample[index]) == 0U ? 1U : 0U;
        oddZeros += value(sample[index + 1]) == 0U ? 1U : 0U;
    }

    if (littleEndian) {
        return oddZeros * 100U >= pairs * 60U && evenZeros * 100U <= pairs * 20U;
    }
    return evenZeros * 100U >= pairs * 60U && oddZeros * 100U <= pairs * 20U;
}

[[nodiscard]] bool looksBinary(std::span<const std::byte> bytes) noexcept {
    if (bytes.empty()) {
        return false;
    }

    std::size_t nulCount = 0;
    std::size_t controlCount = 0;

    for (const auto byte : bytes) {
        const auto current = value(byte);
        if (current == 0U) {
            ++nulCount;
            continue;
        }

        const bool allowedControl = current == 0x09U || current == 0x0AU || current == 0x0DU ||
                                    current == 0x0CU || current == 0x08U;
        if (current < 0x20U && !allowedControl) {
            ++controlCount;
        }
    }

    if (nulCount > 0) {
        return true;
    }

    return controlCount * 100U > bytes.size() * 2U;
}

}

DetectionResult EncodingDetector::detect(const std::span<const std::byte> bytes) noexcept {
    static constexpr std::array<unsigned char, 4> utf32Le{0xFFU, 0xFEU, 0x00U, 0x00U};
    static constexpr std::array<unsigned char, 4> utf32Be{0x00U, 0x00U, 0xFEU, 0xFFU};
    static constexpr std::array<unsigned char, 3> utf8Bom{0xEFU, 0xBBU, 0xBFU};
    static constexpr std::array<unsigned char, 2> utf16Le{0xFFU, 0xFEU};
    static constexpr std::array<unsigned char, 2> utf16Be{0xFEU, 0xFFU};

    if (startsWith(bytes, utf32Le)) {
        return {Encoding::Utf32LE, true, false, false};
    }
    if (startsWith(bytes, utf32Be)) {
        return {Encoding::Utf32BE, true, false, false};
    }
    if (startsWith(bytes, utf8Bom)) {
        const auto payload = bytes.subspan(utf8Bom.size());
        const bool valid = isValidUtf8(payload);
        return {Encoding::Utf8, true, valid, !valid && looksBinary(payload)};
    }
    if (startsWith(bytes, utf16Le)) {
        return {Encoding::Utf16LE, true, false, false};
    }
    if (startsWith(bytes, utf16Be)) {
        return {Encoding::Utf16BE, true, false, false};
    }

    bool hasNul = false;
    bool ascii = true;
    std::size_t controlCount = 0U;
    for (const auto byte : bytes) {
        const auto current = value(byte);
        hasNul = hasNul || current == 0U;
        ascii = ascii && current <= 0x7FU;
        const bool allowedControl = current == 0x09U || current == 0x0AU ||
                                    current == 0x0DU || current == 0x0CU ||
                                    current == 0x08U;
        if (current < 0x20U && current != 0U && !allowedControl) {
            ++controlCount;
        }
    }

    if (!hasNul) {
        if (!bytes.empty() && controlCount * 100U > bytes.size() * 2U) {
            return {Encoding::Binary, false, false, true};
        }
        if (ascii) {
            return {Encoding::Ascii, false, true, false};
        }
        const bool utf8 = isValidUtf8(bytes);
        if (utf8) {
            return {Encoding::Utf8, false, true, false};
        }
        return {Encoding::Unknown8Bit, false, false, false};
    }

    if (looksUtf32(bytes, true)) {
        return {Encoding::Utf32LE, false, false, false};
    }
    if (looksUtf32(bytes, false)) {
        return {Encoding::Utf32BE, false, false, false};
    }
    if (looksUtf16(bytes, true)) {
        return {Encoding::Utf16LE, false, false, false};
    }
    if (looksUtf16(bytes, false)) {
        return {Encoding::Utf16BE, false, false, false};
    }

    const bool binary = looksBinary(bytes);
    if (binary) {
        return {Encoding::Binary, false, false, true};
    }

    const bool utf8 = isValidUtf8(bytes);
    if (utf8) {
        const bool asciiOnly = std::ranges::all_of(bytes, [](const std::byte byte) {
            return value(byte) <= 0x7FU;
        });
        return {asciiOnly ? Encoding::Ascii : Encoding::Utf8, false, true, false};
    }

    return {Encoding::Unknown8Bit, false, false, false};
}

std::string_view EncodingDetector::name(const Encoding encoding) noexcept {
    switch (encoding) {
    case Encoding::Ascii:
        return "ASCII";
    case Encoding::Utf8:
        return "UTF-8";
    case Encoding::Utf16LE:
        return "UTF-16 LE";
    case Encoding::Utf16BE:
        return "UTF-16 BE";
    case Encoding::Utf32LE:
        return "UTF-32 LE";
    case Encoding::Utf32BE:
        return "UTF-32 BE";
    case Encoding::Windows1252:
        return "Windows-1252";
    case Encoding::Windows1254:
        return "Windows-1254";
    case Encoding::Unknown8Bit:
        return "Unknown 8-bit";
    case Encoding::Binary:
        return "Binary";
    }
    return "Unknown";
}

}
