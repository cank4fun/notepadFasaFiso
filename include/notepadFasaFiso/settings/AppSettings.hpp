#pragma once

#include "notepadFasaFiso/core/FileSniffer.hpp"
#include "notepadFasaFiso/recovery/AutoSaveManager.hpp"
#include "notepadFasaFiso/viewer/ViewCache.hpp"

#include <cstdint>
#include <string>

namespace nff::settings {

enum class ThemePreference : std::uint8_t {
    System,
    Light,
    Dark,
};

enum class AccentPreference : std::uint8_t {
    Violet,
    Blue,
    Teal,
    Rose,
    Amber,
};

enum class UiDensity : std::uint8_t {
    Compact,
    Comfortable,
};

struct AppSettings final {
    ThemePreference theme{ThemePreference::Dark};
    AccentPreference accent{AccentPreference::Violet};
    UiDensity density{UiDensity::Comfortable};
    std::string fontFamily;
    double fontPointSize{12.0};
    bool wordWrap{true};
    bool showLineNumbers{false};
    bool highlightUrls{false};
    bool tabsEnabled{true};
    bool sidebarVisible{true};
    std::string sidebarRootUtf8;
    bool restorePreviousSession{true};
    viewer::PerformanceProfile viewerPerformance{viewer::PerformanceProfile::Automatic};
    core::InspectOptions inspectOptions{};
    recovery::AutoSavePolicy autoSave{};
};

[[nodiscard]] bool validate(const AppSettings& settings) noexcept;

}
