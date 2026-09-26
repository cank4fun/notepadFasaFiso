#include "notepadFasaFiso/settings/AppSettings.hpp"

#include <cmath>
#include <cstddef>

namespace nff::settings {
namespace {

constexpr std::size_t maximumInspectionSampleBytes = 64U * 1024U * 1024U;
constexpr std::size_t maximumLongLineThresholdBytes = 64U * 1024U * 1024U;

}

bool validate(const AppSettings& settings) noexcept {
    if (static_cast<std::uint8_t>(settings.theme) >
            static_cast<std::uint8_t>(ThemePreference::Dark) ||
        static_cast<std::uint8_t>(settings.accent) >
            static_cast<std::uint8_t>(AccentPreference::Amber) ||
        static_cast<std::uint8_t>(settings.density) >
            static_cast<std::uint8_t>(UiDensity::Comfortable)) {
        return false;
    }
    if (settings.fontFamily.size() > 16U * 1024U || settings.sidebarRootUtf8.size() > 64U * 1024U ||
        !std::isfinite(settings.fontPointSize) ||
        settings.fontPointSize < 4.0 || settings.fontPointSize > 256.0) {
        return false;
    }
    if (settings.inspectOptions.viewerSizeThreshold == 0U ||
        settings.inspectOptions.longLineThreshold == 0U ||
        settings.inspectOptions.sampleBytes == 0U ||
        settings.inspectOptions.longLineThreshold > maximumLongLineThresholdBytes ||
        settings.inspectOptions.sampleBytes > maximumInspectionSampleBytes) {
        return false;
    }
    if (settings.autoSave.recoveryDelay.count() < 0 || settings.autoSave.saveDelay.count() < 0) {
        return false;
    }
    return true;
}

}
