#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace nff::persistence {

using Magic = std::array<std::byte, 8>;

class BinaryWriter final {
public:
    void writeU8(std::uint8_t value);
    void writeU32(std::uint32_t value);
    void writeU64(std::uint64_t value);
    void writeDouble(double value);
    void writeBool(bool value);
    void writeString(std::string_view value);
    void writePath(const std::filesystem::path& value);
    void writeBytes(std::span<const std::byte> value);

    [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept;
    [[nodiscard]] std::vector<std::byte> take() noexcept;

private:
    std::vector<std::byte> bytes_;
};

class BinaryReader final {
public:
    explicit BinaryReader(std::span<const std::byte> bytes) noexcept;

    [[nodiscard]] bool readU8(std::uint8_t& value) noexcept;
    [[nodiscard]] bool readU32(std::uint32_t& value) noexcept;
    [[nodiscard]] bool readU64(std::uint64_t& value) noexcept;
    [[nodiscard]] bool readDouble(double& value) noexcept;
    [[nodiscard]] bool readBool(bool& value) noexcept;
    [[nodiscard]] bool readString(std::string& value, std::size_t maximumBytes);
    [[nodiscard]] bool readPath(std::filesystem::path& value, std::size_t maximumBytes);
    [[nodiscard]] bool readBytes(std::vector<std::byte>& value, std::size_t maximumBytes);

    [[nodiscard]] std::size_t remaining() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

private:
    std::span<const std::byte> bytes_;
    std::size_t offset_{0};
};

struct Envelope final {
    std::uint32_t schemaVersion{0};
    std::vector<std::byte> payload;
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

[[nodiscard]] std::error_code writeEnvelope(const std::filesystem::path& path,
                                            const Magic& magic,
                                            std::uint32_t schemaVersion,
                                            std::span<const std::byte> payload);
[[nodiscard]] Envelope readEnvelope(const std::filesystem::path& path,
                                    const Magic& magic,
                                    std::size_t maximumPayloadBytes);
[[nodiscard]] std::uint64_t hash64(std::span<const std::byte> bytes) noexcept;
[[nodiscard]] std::uint64_t hash64(std::string_view text) noexcept;

}
