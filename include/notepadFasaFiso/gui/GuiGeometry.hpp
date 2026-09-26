#pragma once

#include <cmath>

namespace nff::gui {

struct Point final {
    double x{0.0};
    double y{0.0};
};

struct Size final {
    double width{0.0};
    double height{0.0};
};

struct Rect final {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};

    [[nodiscard]] double right() const noexcept { return x + width; }
    [[nodiscard]] double bottom() const noexcept { return y + height; }
    [[nodiscard]] bool empty() const noexcept { return width <= 0.0 || height <= 0.0; }
    [[nodiscard]] bool contains(const Point point) const noexcept {
        return !empty() && point.x >= x && point.y >= y &&
               point.x < right() && point.y < bottom();
    }
};

[[nodiscard]] inline bool finite(const Point point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] inline bool finite(const Size size) noexcept {
    return std::isfinite(size.width) && std::isfinite(size.height);
}

[[nodiscard]] inline bool finite(const Rect rect) noexcept {
    return std::isfinite(rect.x) && std::isfinite(rect.y) &&
           std::isfinite(rect.width) && std::isfinite(rect.height);
}

}
