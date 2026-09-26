#include "notepadFasaFiso/gui/ColorWheelModel.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nff::gui {
namespace {

[[nodiscard]] double clampUnit(const double value) noexcept {
    return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] std::uint8_t channel(const double value) noexcept {
    return static_cast<std::uint8_t>(std::clamp(std::lround(clampUnit(value) * 255.0), 0L, 255L));
}

}

HsvColor rgbToHsv(const RgbColor color) noexcept {
    const double red = static_cast<double>(color.red) / 255.0;
    const double green = static_cast<double>(color.green) / 255.0;
    const double blue = static_cast<double>(color.blue) / 255.0;
    const double maximum = std::max({red, green, blue});
    const double minimum = std::min({red, green, blue});
    const double delta = maximum - minimum;

    HsvColor result;
    result.value = maximum;
    result.saturation = maximum <= 0.0 ? 0.0 : delta / maximum;
    if (delta <= 0.0) {
        result.hue = 0.0;
    } else if (maximum == red) {
        result.hue = 60.0 * std::fmod((green - blue) / delta, 6.0);
    } else if (maximum == green) {
        result.hue = 60.0 * (((blue - red) / delta) + 2.0);
    } else {
        result.hue = 60.0 * (((red - green) / delta) + 4.0);
    }
    if (result.hue < 0.0) {
        result.hue += 360.0;
    }
    return result;
}

RgbColor hsvToRgb(HsvColor color) noexcept {
    color.saturation = clampUnit(color.saturation);
    color.value = clampUnit(color.value);
    color.hue = std::fmod(color.hue, 360.0);
    if (color.hue < 0.0) {
        color.hue += 360.0;
    }

    const double chroma = color.value * color.saturation;
    const double hueSection = color.hue / 60.0;
    const double x = chroma * (1.0 - std::abs(std::fmod(hueSection, 2.0) - 1.0));
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    if (hueSection < 1.0) { red = chroma; green = x; }
    else if (hueSection < 2.0) { red = x; green = chroma; }
    else if (hueSection < 3.0) { green = chroma; blue = x; }
    else if (hueSection < 4.0) { green = x; blue = chroma; }
    else if (hueSection < 5.0) { red = x; blue = chroma; }
    else { red = chroma; blue = x; }

    const double match = color.value - chroma;
    return {channel(red + match), channel(green + match), channel(blue + match)};
}

HsvColor colorWheelPointToHsv(const double x,
                              const double y,
                              const double centerX,
                              const double centerY,
                              const double radius,
                              const double value) noexcept {
    if (!std::isfinite(radius) || radius <= 0.0) {
        return {0.0, 0.0, clampUnit(value)};
    }
    const double dx = x - centerX;
    const double dy = y - centerY;
    const double distance = std::hypot(dx, dy);
    double hue = std::atan2(-dy, dx) * 180.0 / std::numbers::pi;
    if (hue < 0.0) {
        hue += 360.0;
    }
    return {hue, std::min(distance / radius, 1.0), clampUnit(value)};
}

}
