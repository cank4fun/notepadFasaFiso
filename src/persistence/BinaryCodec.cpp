#include "notepadFasaFiso/persistence/BinaryCodec.hpp"

#include "notepadFasaFiso/encoding/TextCodec.hpp"
#include "notepadFasaFiso/storage/FileReader.hpp"
#include "notepadFasaFiso/storage/FileWriter.hpp"
#include "notepadFasaFiso/platform/Platform.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <type_traits>
#include <utility>

namespace nff::persistence {
namespace {

constexpr std::size_t envelopeHeaderBytes = 8U + 4U + 8U + 8U;

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::filesystem::path pathFromUtf8(const std::string_view value) {
    std::u8string converted;
    converted.reserve(value.size());
    for (const auto character : value) {
        converted.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    }
    return std::filesystem::path(converted);
}

template <typename Integer>
void appendLittleEndian(std::vector<std::byte>& output, const Integer value) {
    using Unsigned = std::make_unsigned_t<Integer>;
    auto current = static_cast<std::uint64_t>(static_cast<Unsigned>(value));
    for (std::size_t index = 0; index < sizeof(Unsigned); ++index) {
        output.push_back(static_cast<std::byte>(current & 0xFFU));
        current >>= 8U;
    }
}

template <typename Integer>
[[nodiscard]] bool readLittleEndian(const std::span<const std::byte> bytes,
                                    std::size_t& offset,
                                    Integer& output) noexcept {
    using Unsigned = std::make_unsigned_t<Integer>;
    if (offset > bytes.size() || bytes.size() - offset < sizeof(Unsigned)) {
        return false;
    }

    std::uint64_t value = 0;
    for (std::size_t index = 0; index < sizeof(Unsigned); ++index) {
        const auto byte = std::to_integer<std::uint8_t>(bytes[offset + index]);
        value |= static_cast<std::uint64_t>(byte) << static_cast<unsigned int>(index * 8U);
    }
    offset += sizeof(Unsigned);
    output = static_cast<Integer>(static_cast<Unsigned>(value));
    return true;
}

}

void BinaryWriter::writeU8(const std::uint8_t value) {
    bytes_.push_back(static_cast<std::byte>(value));
}

void BinaryWriter::writeU32(const std::uint32_t value) {
    appendLittleEndian(bytes_, value);
}

void BinaryWriter::writeU64(const std::uint64_t value) {
    appendLittleEndian(bytes_, value);
}

void BinaryWriter::writeDouble(const double value) {
    writeU64(std::bit_cast<std::uint64_t>(value));
}

void BinaryWriter::writeBool(const bool value) {
    writeU8(value ? 1U : 0U);
}

void BinaryWriter::writeString(const std::string_view value) {
    writeU64(static_cast<std::uint64_t>(value.size()));
    const auto* begin = reinterpret_cast<const std::byte*>(value.data());
    bytes_.insert(bytes_.end(), begin, begin + value.size());
}

void BinaryWriter::writePath(const std::filesystem::path& value) {
    writeString(pathToUtf8(value));
}

void BinaryWriter::writeBytes(const std::span<const std::byte> value) {
    writeU64(static_cast<std::uint64_t>(value.size()));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
}

const std::vector<std::byte>& BinaryWriter::bytes() const noexcept {
    return bytes_;
}

std::vector<std::byte> BinaryWriter::take() noexcept {
    return std::move(bytes_);
}

BinaryReader::BinaryReader(const std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}

bool BinaryReader::readU8(std::uint8_t& value) noexcept {
    if (offset_ >= bytes_.size()) {
        return false;
    }
    value = std::to_integer<std::uint8_t>(bytes_[offset_++]);
    return true;
}

bool BinaryReader::readU32(std::uint32_t& value) noexcept {
    return readLittleEndian(bytes_, offset_, value);
}

bool BinaryReader::readU64(std::uint64_t& value) noexcept {
    return readLittleEndian(bytes_, offset_, value);
}

bool BinaryReader::readDouble(double& value) noexcept {
    std::uint64_t bits = 0;
    if (!readU64(bits)) {
        return false;
    }
    value = std::bit_cast<double>(bits);
    return true;
}

bool BinaryReader::readBool(bool& value) noexcept {
    std::uint8_t encoded = 0;
    if (!readU8(encoded) || encoded > 1U) {
        return false;
    }
    value = encoded != 0U;
    return true;
}

bool BinaryReader::readString(std::string& value, const std::size_t maximumBytes) {
    std::uint64_t length = 0;
    if (!readU64(length) || length > static_cast<std::uint64_t>(maximumBytes) ||
        length > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    const auto size = static_cast<std::size_t>(length);
    if (offset_ > bytes_.size() || bytes_.size() - offset_ < size) {
        return false;
    }
    const auto* begin = reinterpret_cast<const char*>(bytes_.data() + offset_);
    value.assign(begin, size);
    offset_ += size;
    return true;
}

bool BinaryReader::readPath(std::filesystem::path& value, const std::size_t maximumBytes) {
    std::string encoded;
    if (!readString(encoded, maximumBytes) || !encoding::TextCodec::isValidUtf8(encoded)) {
        return false;
    }
    value = pathFromUtf8(encoded);
    return true;
}

bool BinaryReader::readBytes(std::vector<std::byte>& value, const std::size_t maximumBytes) {
    std::uint64_t length = 0;
    if (!readU64(length) || length > static_cast<std::uint64_t>(maximumBytes) ||
        length > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    const auto size = static_cast<std::size_t>(length);
    if (offset_ > bytes_.size() || bytes_.size() - offset_ < size) {
        return false;
    }
    const auto first = bytes_.begin() + static_cast<std::ptrdiff_t>(offset_);
    value.assign(first, first + static_cast<std::ptrdiff_t>(size));
    offset_ += size;
    return true;
}

std::size_t BinaryReader::remaining() const noexcept {
    return offset_ <= bytes_.size() ? bytes_.size() - offset_ : 0U;
}

bool BinaryReader::empty() const noexcept {
    return remaining() == 0U;
}

std::error_code writeEnvelope(const std::filesystem::path& path,
                              const Magic& magic,
                              const std::uint32_t schemaVersion,
                              const std::span<const std::byte> payload) {
    if (path.empty() || schemaVersion == 0U) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    if (!path.parent_path().empty()) {
        const auto parent = path.parent_path();
        std::error_code directoryError;
        const bool parentExists = std::filesystem::exists(parent, directoryError);
        if (directoryError) {
            return directoryError;
        }

        if (!parentExists) {
            if (const auto createError = platform::ensurePrivateDirectory(parent)) {
                return createError;
            }
        } else if (!std::filesystem::is_directory(parent, directoryError)) {
            if (directoryError) {
                return directoryError;
            }
            return std::make_error_code(std::errc::not_a_directory);
        }
    }

    std::vector<std::byte> bytes;
    bytes.reserve(envelopeHeaderBytes + payload.size());
    bytes.insert(bytes.end(), magic.begin(), magic.end());
    appendLittleEndian(bytes, schemaVersion);
    appendLittleEndian(bytes, static_cast<std::uint64_t>(payload.size()));
    appendLittleEndian(bytes, hash64(payload));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    if (const auto writeError = storage::FileWriter::writeAtomically(path, bytes)) {
        return writeError;
    }
    return platform::hardenPrivateFile(path);
}

Envelope readEnvelope(const std::filesystem::path& path,
                      const Magic& magic,
                      const std::size_t maximumPayloadBytes) {
    if (maximumPayloadBytes > std::numeric_limits<std::size_t>::max() - envelopeHeaderBytes) {
        return {{}, {}, std::make_error_code(std::errc::value_too_large)};
    }

    const auto read = storage::FileReader::readAll(path, maximumPayloadBytes + envelopeHeaderBytes);
    if (!read) {
        return {{}, {}, read.error};
    }
    if (read.bytes.size() < envelopeHeaderBytes ||
        !std::equal(magic.begin(), magic.end(), read.bytes.begin())) {
        return {{}, {}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }

    const auto input = std::span<const std::byte>(read.bytes);
    std::size_t offset = magic.size();
    std::uint32_t version = 0;
    std::uint64_t payloadSize = 0;
    std::uint64_t checksum = 0;
    if (!readLittleEndian(input, offset, version) ||
        !readLittleEndian(input, offset, payloadSize) ||
        !readLittleEndian(input, offset, checksum) || version == 0U ||
        payloadSize > static_cast<std::uint64_t>(maximumPayloadBytes) ||
        payloadSize != static_cast<std::uint64_t>(input.size() - offset)) {
        return {{}, {}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }

    const auto payload = input.subspan(offset);
    if (hash64(payload) != checksum) {
        return {{}, {}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }

    return {version, std::vector<std::byte>(payload.begin(), payload.end()), {}};
}

std::uint64_t hash64(const std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : bytes) {
        hash ^= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(byte));
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint64_t hash64(const std::string_view text) noexcept {
    const auto* begin = reinterpret_cast<const std::byte*>(text.data());
    return hash64(std::span<const std::byte>(begin, text.size()));
}

}
