#pragma once

#include "notepadFasaFiso/settings/AppSettings.hpp"

#include <wx/colour.h>

namespace nff::gui::wxbackend {

struct WxThemePalette final {
    wxColour window;
    wxColour frame;
    wxColour chrome;
    wxColour surface;
    wxColour surfaceRaised;
    wxColour editor;
    wxColour text;
    wxColour mutedText;
    wxColour border;
    wxColour accent;
    wxColour accentHover;
    wxColour selection;
    wxColour lineNumber;
    wxColour lineNumberBackground;
    wxColour splitter;
    wxColour danger;
    wxColour warningSurface;
    wxColour warningText;
    wxColour recoverySurface;
    wxColour recoveryText;
    bool dark{true};
};

[[nodiscard]] WxThemePalette darkTheme(settings::AccentPreference accent);
[[nodiscard]] WxThemePalette lightTheme(settings::AccentPreference accent);
[[nodiscard]] WxThemePalette themeFor(settings::ThemePreference preference,
                                      settings::AccentPreference accent);

}
