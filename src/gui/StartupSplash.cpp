#include "notepadFasaFiso/gui/StartupSplash.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace nff::gui {
namespace {
constexpr double kAspect = 9.0 / 5.0;
constexpr double kPreferredWidth = 900.0;
constexpr double kMaximumWidth = 960.0;
constexpr double kMaximumHeight = 540.0;
constexpr double kDisplayWidthFraction = 0.90;
constexpr double kDisplayHeightFraction = 0.78;
constexpr std::string_view kVectorAlphabet = " 'Fadehimnopsty";

[[nodiscard]] double safeExtent(const double value) noexcept {
    return std::isfinite(value) && value > 0.0 ? value : 1.0;
}
}

StartupSplashLayout startupSplashLayout(const Size availableDisplay) noexcept {
    const double displayWidth = safeExtent(availableDisplay.width);
    const double displayHeight = safeExtent(availableDisplay.height);
    const double widthLimit = std::min(kMaximumWidth, displayWidth * kDisplayWidthFraction);
    const double heightLimit = std::min(kMaximumHeight, displayHeight * kDisplayHeightFraction);
    const double width = std::max(1.0, std::min({kPreferredWidth, widthLimit, heightLimit * kAspect}));
    const double height = width / kAspect;

    StartupSplashLayout layout;
    layout.window = {0.0, 0.0, width, height};
    layout.strokeWidth = std::max(1.0, width * 0.0015);
    const double borderInset = layout.strokeWidth * 1.25;
    layout.panel = {borderInset, borderInset, width - borderInset * 2.0, height - borderInset * 2.0};
    layout.wordmark = {width * 0.115, height * 0.385, width * 0.77, height * 0.155};
    layout.tagline = {width * 0.285, height * 0.585, width * 0.43, height * 0.055};
    layout.indicator = {width * 0.39, height * 0.80, width * 0.22, height * 0.022};
    layout.cornerRadius = std::max(8.0, width * 0.015);
    return layout;
}

bool startupSplashCanRender(const std::string_view text) noexcept {
    return std::all_of(text.begin(), text.end(), [](const char character) {
        return kVectorAlphabet.find(character) != std::string_view::npos;
    });
}

}
