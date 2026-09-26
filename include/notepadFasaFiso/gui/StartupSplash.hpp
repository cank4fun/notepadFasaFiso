#pragma once

#include "notepadFasaFiso/gui/GuiGeometry.hpp"

#include <string_view>

namespace nff::gui {

struct StartupSplashLayout final {
    Rect window{};
    Rect panel{};
    Rect wordmark{};
    Rect tagline{};
    Rect indicator{};
    double cornerRadius{0.0};
    double strokeWidth{0.0};
};

[[nodiscard]] StartupSplashLayout startupSplashLayout(Size availableDisplay) noexcept;
[[nodiscard]] bool startupSplashCanRender(std::string_view text) noexcept;

}
