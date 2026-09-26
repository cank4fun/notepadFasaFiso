#pragma once

#include "notepadFasaFiso/core/FileSniffer.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace nff::history {

struct RecentFileEntry final {
    std::filesystem::path path;
    std::uint64_t lastOpenedUnixMilliseconds{0};
    std::uint64_t openCount{0};
    core::OpenMode lastOpenMode{core::OpenMode::Editor};
};

class RecentFiles final {
public:
    static constexpr std::size_t defaultCapacity = 128U;
    static constexpr std::size_t maximumCapacity = 4096U;

    explicit RecentFiles(std::size_t capacity = defaultCapacity) noexcept;

    void setCapacity(std::size_t capacity) noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] std::span<const RecentFileEntry> entries() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

    void touch(const std::filesystem::path& path,
               core::OpenMode mode,
               std::uint64_t unixMilliseconds = 0);
    [[nodiscard]] bool remove(const std::filesystem::path& path);
    std::size_t pruneMissing();
    void clear() noexcept;

    [[nodiscard]] bool replace(std::vector<RecentFileEntry> entries);

private:
    [[nodiscard]] static bool samePath(const std::filesystem::path& left,
                                       const std::filesystem::path& right);
    [[nodiscard]] static std::uint64_t nowUnixMilliseconds() noexcept;
    void trimToCapacity() noexcept;

    std::size_t capacity_{defaultCapacity};
    std::vector<RecentFileEntry> entries_;
};

}
