#include "WxTheme.hpp"

#include <wx/settings.h>
#include <wx/version.h>

namespace nff::gui::wxbackend {
namespace {

struct AccentColours final {
    wxColour primary;
    wxColour hover;
    wxColour selectionDark;
    wxColour selectionLight;
};

[[nodiscard]] AccentColours accentColours(const settings::AccentPreference accent) {
    switch (accent) {
    case settings::AccentPreference::Violet:
        return {wxColour(139, 92, 246), wxColour(159, 122, 234),
                wxColour(67, 52, 104), wxColour(220, 211, 247)};
    case settings::AccentPreference::Blue:
        return {wxColour(59, 130, 246), wxColour(96, 165, 250),
                wxColour(38, 67, 105), wxColour(207, 226, 252)};
    case settings::AccentPreference::Teal:
        return {wxColour(20, 184, 166), wxColour(45, 212, 191),
                wxColour(30, 82, 79), wxColour(199, 240, 235)};
    case settings::AccentPreference::Rose:
        return {wxColour(244, 63, 94), wxColour(251, 113, 133),
                wxColour(96, 43, 57), wxColour(251, 211, 218)};
    case settings::AccentPreference::Amber:
        return {wxColour(245, 158, 11), wxColour(251, 191, 36),
                wxColour(94, 70, 31), wxColour(250, 229, 190)};
    }
    return accentColours(settings::AccentPreference::Violet);
}

}

WxThemePalette darkTheme(const settings::AccentPreference accent) {
    const auto accentPalette = accentColours(accent);
    return {
        wxColour(15, 15, 20),
        wxColour(22, 22, 29),
        wxColour(19, 19, 25),
        wxColour(27, 27, 35),
        wxColour(36, 36, 46),
        wxColour(13, 13, 18),
        wxColour(238, 238, 244),
        wxColour(154, 154, 170),
        wxColour(45, 45, 58),
        accentPalette.primary,
        accentPalette.hover,
        accentPalette.selectionDark,
        wxColour(126, 126, 143),
        wxColour(21, 21, 28),
        wxColour(58, 58, 72),
        wxColour(224, 108, 117),
        wxColour(52, 40, 30),
        wxColour(232, 190, 118),
        wxColour(34, 43, 52),
        wxColour(151, 198, 235),
        true,
    };
}

WxThemePalette lightTheme(const settings::AccentPreference accent) {
    const auto accentPalette = accentColours(accent);
    return {
        wxColour(239, 240, 244),
        wxColour(229, 231, 237),
        wxColour(234, 235, 240),
        wxColour(246, 247, 249),
        wxColour(252, 252, 253),
        wxColour(249, 250, 252),
        wxColour(31, 31, 38),
        wxColour(96, 97, 112),
        wxColour(202, 204, 214),
        accentPalette.primary,
        accentPalette.hover,
        accentPalette.selectionLight,
        wxColour(108, 109, 124),
        wxColour(241, 242, 246),
        wxColour(184, 186, 198),
        wxColour(190, 70, 82),
        wxColour(255, 246, 228),
        wxColour(132, 86, 22),
        wxColour(232, 244, 252),
        wxColour(39, 103, 145),
        false,
    };
}

WxThemePalette themeFor(const settings::ThemePreference preference,
                        const settings::AccentPreference accent) {
    switch (preference) {
    case settings::ThemePreference::Dark:
        return darkTheme(accent);
    case settings::ThemePreference::Light:
        return lightTheme(accent);
    case settings::ThemePreference::System:
#if wxCHECK_VERSION(3, 3, 0)
        return wxSystemSettings::GetAppearance().IsSystemDark() ? darkTheme(accent)
                                                               : lightTheme(accent);
#else
        return darkTheme(accent);
#endif
    }
    return darkTheme(accent);
}

}
