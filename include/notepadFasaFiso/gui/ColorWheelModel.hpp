#pragma once

#include <cstdint>

namespace nff::gui {

struct RgbColor final {
    std::uint8_t red{0U};
    std::uint8_t green{0U};
    std::uint8_t blue{0U};
    friend bool operator==(const RgbColor&, const RgbColor&) = default;
};

struct HsvColor final {
    double hue{0.0};
    double saturation{0.0};
    double value{0.0};
};

[[nodiscard]] HsvColor rgbToHsv(RgbColor color) noexcept;
[[nodiscard]] RgbColor hsvToRgb(HsvColor color) noexcept;
[[nodiscard]] HsvColor colorWheelPointToHsv(double x,
                                             double y,
                                             double centerX,
                                             double centerY,
                                             double radius,
                                             double value) noexcept;

}
