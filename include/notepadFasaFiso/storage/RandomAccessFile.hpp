#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <system_error>

namespace nff::storage {

struct RandomReadResult final {
    std::size_t bytesRead{};
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

struct FileIdentity final {
    std::uint64_t primary{};
    std::uint64_t secondary{};
    std::uint64_t tertiary{};
    bool valid{false};

    [[nodiscard]] friend constexpr bool operator==(const FileIdentity&,
                                                   const FileIdentity&) noexcept = default;
};

class RandomAccessFile final {
public:
    RandomAccessFile();
    ~RandomAccessFile();

    RandomAccessFile(const RandomAccessFile&) = delete;
    RandomAccessFile& operator=(const RandomAccessFile&) = delete;
    RandomAccessFile(RandomAccessFile&&) noexcept;
    RandomAccessFile& operator=(RandomAccessFile&&) noexcept;

    [[nodiscard]] std::error_code open(const std::filesystem::path& path);
    void close() noexcept;

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] std::uint64_t size() const noexcept;
    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] FileIdentity identity() const noexcept;

    [[nodiscard]] RandomReadResult readAt(std::uint64_t offset,
                                          std::span<std::byte> destination) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::filesystem::path path_;
    std::uint64_t size_{};
    FileIdentity identity_{};
};

}
