#pragma once

#include "notepadFasaFiso/settings/AppSettings.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <system_error>

namespace nff::settings {

struct SettingsLoadResult final {
    AppSettings settings;
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

class SettingsStore final {
public:
    static constexpr std::uint32_t currentSchemaVersion = 5U;
    static constexpr std::size_t maximumSettingsBytes = 1024U * 1024U;

    [[nodiscard]] static std::error_code save(const std::filesystem::path& path,
                                              const AppSettings& settings);
    [[nodiscard]] static SettingsLoadResult load(const std::filesystem::path& path);
};

}
