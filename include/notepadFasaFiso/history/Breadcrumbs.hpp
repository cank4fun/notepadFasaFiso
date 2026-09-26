#pragma once

#include "notepadFasaFiso/history/RecentFiles.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <system_error>

namespace nff::history {

struct RecentFilesLoadResult final {
    RecentFiles recent;
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

class Breadcrumbs final {
public:
    static constexpr std::uint32_t currentSchemaVersion = 1U;
    static constexpr std::size_t maximumStoreBytes = 8U * 1024U * 1024U;

    [[nodiscard]] static std::error_code save(const std::filesystem::path& path,
                                              const RecentFiles& recent);
    [[nodiscard]] static RecentFilesLoadResult load(const std::filesystem::path& path,
                                                    std::size_t capacity = RecentFiles::defaultCapacity);
};

}
