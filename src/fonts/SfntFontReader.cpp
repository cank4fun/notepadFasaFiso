#include "notepadFasaFiso/fonts/SfntFontReader.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nff::fonts {
namespace {

[[nodiscard]] std::uint16_t readU16(const std::span<const std::byte> data,
                                    const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[offset]) << 8U) |
        static_cast<std::uint16_t>(data[offset + 1U]));
}

[[nodiscard]] std::uint32_t readU32(const std::span<const std::byte> data,
                                    const std::size_t offset) noexcept {
    return (static_cast<std::uint32_t>(data[offset]) << 24U) |
           (static_cast<std::uint32_t>(data[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(data[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(data[offset + 3U]);
}

[[nodiscard]] bool readAt(std::ifstream& stream,
                          const std::uint64_t offset,
                          const std::size_t size,
                          std::vector<std::byte>& output) {
    if (size > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()) ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        return false;
    }
    output.resize(size);
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream) {
        return false;
    }
    if (size == 0U) {
        return true;
    }
    stream.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(size));
    return stream.gcount() == static_cast<std::streamsize>(size);
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
    } else if (codePoint <= 0x10FFFFU) {
        output.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    }
}

[[nodiscard]] std::string decodeUtf16Be(const std::span<const std::byte> bytes) {
    if ((bytes.size() & 1U) != 0U) {
        return {};
    }
    std::string output;
    output.reserve(bytes.size());
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto first = readU16(bytes, offset);
        offset += 2U;
        std::uint32_t codePoint = first;
        if (first >= 0xD800U && first <= 0xDBFFU) {
            if (offset + 2U > bytes.size()) {
                return {};
            }
            const auto second = readU16(bytes, offset);
            offset += 2U;
            if (second < 0xDC00U || second > 0xDFFFU) {
                return {};
            }
            codePoint = 0x10000U +
                        ((static_cast<std::uint32_t>(first) - 0xD800U) << 10U) +
                        (static_cast<std::uint32_t>(second) - 0xDC00U);
        } else if (first >= 0xDC00U && first <= 0xDFFFU) {
            return {};
        }
        appendUtf8(output, codePoint);
    }
    return output;
}

[[nodiscard]] std::string decodeAsciiCompatible(const std::span<const std::byte> bytes) {
    std::string output;
    output.reserve(bytes.size());
    for (const auto value : bytes) {
        const auto byte = static_cast<unsigned char>(value);
        if (byte >= 0x80U) {
            return {};
        }
        output.push_back(static_cast<char>(byte));
    }
    return output;
}

void trimName(std::string& value) {
    while (!value.empty() && (value.back() == '\0' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    auto begin = value.begin();
    while (begin != value.end() && (*begin == ' ' || *begin == '\t')) {
        ++begin;
    }
    value.erase(value.begin(), begin);
}

struct NameCandidate final {
    int score{-1};
    std::string value;
};

[[nodiscard]] NameCandidate parseNameTable(std::ifstream& stream,
                                           const std::uint64_t tableOffset,
                                           const std::uint32_t tableLength,
                                           const std::uint64_t fileSize) {
    constexpr std::uint32_t maxNameTableBytes = 16U * 1024U * 1024U;
    if (tableLength < 6U || tableLength > maxNameTableBytes ||
        tableOffset > fileSize || tableLength > fileSize - tableOffset) {
        return {};
    }

    std::vector<std::byte> table;
    if (!readAt(stream, tableOffset, static_cast<std::size_t>(tableLength), table)) {
        return {};
    }
    const std::span<const std::byte> data(table);
    const auto count = static_cast<std::size_t>(readU16(data, 2U));
    const auto storageOffset = static_cast<std::size_t>(readU16(data, 4U));
    if (count > (data.size() - 6U) / 12U || storageOffset > data.size()) {
        return {};
    }

    NameCandidate best;
    for (std::size_t index = 0U; index < count; ++index) {
        const auto record = 6U + index * 12U;
        const auto platformId = readU16(data, record);
        const auto languageId = readU16(data, record + 4U);
        const auto nameId = readU16(data, record + 6U);
        const auto length = static_cast<std::size_t>(readU16(data, record + 8U));
        const auto relativeOffset = static_cast<std::size_t>(readU16(data, record + 10U));
        if (nameId != 1U && nameId != 16U) {
            continue;
        }
        if (relativeOffset > data.size() - storageOffset ||
            length > data.size() - storageOffset - relativeOffset) {
            continue;
        }

        const auto text = data.subspan(storageOffset + relativeOffset, length);
        std::string decoded;
        if (platformId == 0U || platformId == 3U) {
            decoded = decodeUtf16Be(text);
        } else if (platformId == 1U) {
            decoded = decodeAsciiCompatible(text);
        } else {
            continue;
        }
        trimName(decoded);
        if (decoded.empty()) {
            continue;
        }

        int score = nameId == 16U ? 100 : 80;
        if (platformId == 3U) {
            score += 20;
        } else if (platformId == 0U) {
            score += 15;
        }
        if (languageId == 0x0409U || languageId == 0U) {
            score += 10;
        }
        if (score > best.score) {
            best.score = score;
            best.value = std::move(decoded);
        }
    }
    return best;
}

[[nodiscard]] std::string familyAt(std::ifstream& stream,
                                   const std::uint64_t sfntOffset,
                                   const std::uint64_t fileSize) {
    std::vector<std::byte> header;
    if (sfntOffset > fileSize || fileSize - sfntOffset < 12U ||
        !readAt(stream, sfntOffset, 12U, header)) {
        return {};
    }
    const std::span<const std::byte> headerSpan(header);
    const auto tableCount = static_cast<std::size_t>(readU16(headerSpan, 4U));
    if (tableCount == 0U || tableCount > 4096U) {
        return {};
    }

    const auto directoryBytes = tableCount * 16U;
    if (directoryBytes > fileSize - sfntOffset - 12U) {
        return {};
    }
    std::vector<std::byte> directory;
    if (!readAt(stream, sfntOffset + 12U, directoryBytes, directory)) {
        return {};
    }
    const std::span<const std::byte> directorySpan(directory);

    for (std::size_t index = 0U; index < tableCount; ++index) {
        const auto record = index * 16U;
        if (static_cast<char>(directorySpan[record]) != 'n' ||
            static_cast<char>(directorySpan[record + 1U]) != 'a' ||
            static_cast<char>(directorySpan[record + 2U]) != 'm' ||
            static_cast<char>(directorySpan[record + 3U]) != 'e') {
            continue;
        }
        const auto tableOffset = static_cast<std::uint64_t>(readU32(directorySpan, record + 8U));
        const auto tableLength = readU32(directorySpan, record + 12U);
        auto candidate = parseNameTable(stream, tableOffset, tableLength, fileSize);
        return std::move(candidate.value);
    }
    return {};
}

[[nodiscard]] bool equalsInsensitive(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        auto a = static_cast<unsigned char>(left[index]);
        auto b = static_cast<unsigned char>(right[index]);
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<unsigned char>(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = static_cast<unsigned char>(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

}

FontFileInspection SfntFontReader::inspect(const std::filesystem::path& path) {
    std::error_code filesystemError;
    const auto fileSize = std::filesystem::file_size(path, filesystemError);
    if (filesystemError || fileSize < 12U) {
        return {{}, filesystemError ? filesystemError : std::make_error_code(std::errc::invalid_argument)};
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {{}, std::make_error_code(std::errc::io_error)};
    }

    std::vector<std::byte> signature;
    if (!readAt(stream, 0U, 12U, signature)) {
        return {{}, std::make_error_code(std::errc::io_error)};
    }

    FontFileInspection result;
    const std::span<const std::byte> signatureSpan(signature);
    const bool collection = static_cast<char>(signatureSpan[0]) == 't' &&
                            static_cast<char>(signatureSpan[1]) == 't' &&
                            static_cast<char>(signatureSpan[2]) == 'c' &&
                            static_cast<char>(signatureSpan[3]) == 'f';
    if (!collection) {
        auto family = familyAt(stream, 0U, fileSize);
        if (family.empty()) {
            result.error = std::make_error_code(std::errc::invalid_argument);
            return result;
        }
        result.families.push_back(std::move(family));
        return result;
    }

    const auto fontCount = static_cast<std::size_t>(readU32(signatureSpan, 8U));
    if (fontCount == 0U || fontCount > 4096U ||
        fontCount > (fileSize - 12U) / 4U) {
        result.error = std::make_error_code(std::errc::invalid_argument);
        return result;
    }

    std::vector<std::byte> offsets;
    if (!readAt(stream, 12U, fontCount * 4U, offsets)) {
        result.error = std::make_error_code(std::errc::io_error);
        return result;
    }
    const std::span<const std::byte> offsetSpan(offsets);
    for (std::size_t index = 0U; index < fontCount; ++index) {
        const auto sfntOffset = static_cast<std::uint64_t>(readU32(offsetSpan, index * 4U));
        auto family = familyAt(stream, sfntOffset, fileSize);
        if (family.empty()) {
            continue;
        }
        const auto duplicate = std::ranges::find_if(result.families, [&](const std::string& value) {
            return equalsInsensitive(value, family);
        });
        if (duplicate == result.families.end()) {
            result.families.push_back(std::move(family));
        }
    }
    if (result.families.empty()) {
        result.error = std::make_error_code(std::errc::invalid_argument);
    }
    return result;
}

}
