#pragma once

#include "notepadFasaFiso/storage/RandomAccessFile.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <system_error>
#include <vector>

namespace nff::viewer {

struct HexWindow final {
    std::vector<std::byte> bytes;
    std::uint64_t startOffset{};
    std::size_t bytesPerRow{16U};
    std::uint64_t firstRow{};
    std::uint64_t rowCount{};
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
    [[nodiscard]] std::span<const std::byte> row(std::uint64_t relativeRow) const noexcept;
};

class HexPreview final {
public:
    static constexpr std::size_t defaultBytesPerRow = 16U;
    static constexpr std::size_t maximumWindowBytes = 4U * 1024U * 1024U;

    [[nodiscard]] std::error_code open(const std::filesystem::path& path);
    void close() noexcept;

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] std::uint64_t size() const noexcept;
    [[nodiscard]] std::uint64_t rowCount(
        std::size_t bytesPerRow = defaultBytesPerRow) const noexcept;
    [[nodiscard]] const std::filesystem::path& path() const noexcept;

    [[nodiscard]] HexWindow readRows(std::uint64_t firstRow,
                                     std::uint64_t requestedRows,
                                     std::size_t bytesPerRow = defaultBytesPerRow,
                                     std::size_t maximumBytes = maximumWindowBytes) const;

    [[nodiscard]] static char printableAscii(std::byte value) noexcept;

private:
    storage::RandomAccessFile file_;
};

}
