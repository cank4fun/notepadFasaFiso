#include "notepadFasaFiso/gui/GuiPolishModel.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <system_error>

namespace nff::gui {

TabTopologyHasher::TabTopologyHasher(const bool tabsVisible) noexcept {
    mix(tabsVisible ? 1U : 0U);
}

void TabTopologyHasher::mix(const std::uint64_t value) noexcept {
    constexpr std::uint64_t prime = 1099511628211ULL;
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        hash_ ^= (value >> shift) & 0xFFU;
        hash_ *= prime;
    }
}

void TabTopologyHasher::add(const std::uint64_t pane,
                            const std::uint64_t view) noexcept {
    mix(pane);
    mix(view);
}

std::uint64_t TabTopologyHasher::value() const noexcept {
    return hash_;
}

std::uint64_t tabTopologyFingerprint(
    const bool tabsVisible,
    const std::span<const TabTopologyEntry> entries) noexcept {
    TabTopologyHasher hasher(tabsVisible);
    for (const auto& entry : entries) {
        hasher.add(entry.pane, entry.view);
    }
    return hasher.value();
}

void PointerDragLatch::begin() noexcept {
    active_ = true;
}

void PointerDragLatch::end() noexcept {
    active_ = false;
}

void PointerDragLatch::cancel() noexcept {
    active_ = false;
}

bool PointerDragLatch::active() const noexcept {
    return active_;
}

void PointerPressLatch::press(const bool insideTarget) noexcept {
    armed_ = insideTarget;
}

bool PointerPressLatch::release(const bool insideTarget) noexcept {
    const bool commit = armed_ && insideTarget;
    armed_ = false;
    return commit;
}

void PointerPressLatch::cancel() noexcept {
    armed_ = false;
}

bool PointerPressLatch::armed() const noexcept {
    return armed_;
}

void TextColorStyleRefreshLatch::invalidateAppearance() noexcept {
    needsRefresh_ = true;
}

void TextColorStyleRefreshLatch::markRendered() noexcept {
    needsRefresh_ = false;
}

bool TextColorStyleRefreshLatch::needsRefresh() const noexcept {
    return needsRefresh_;
}

PixelBounds pixelBounds(const Rect bounds) noexcept {
    const auto toPixel = [](const double value) noexcept {
        return static_cast<int>(std::max(0.0, value));
    };
    return {toPixel(bounds.x), toPixel(bounds.y), toPixel(bounds.width), toPixel(bounds.height)};
}

std::string formatPointSizeForUi(const double pointSize) {
    std::array<char, 64> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), pointSize);
    if (error != std::errc{}) {
        return "0";
    }
    return std::string(buffer.data(), end);
}

std::string statusValueForUi(const std::string_view label,
                             const std::string_view value) {
    if (label.empty()) {
        return std::string(value);
    }
    if (value.empty()) {
        return std::string(label);
    }
    return std::string(label) + ": " + std::string(value);
}

PaneDropBorder paneDropBorder(const Rect bounds,
                              const double thickness,
                              const double inset) noexcept {
    const double safeInset = std::max(0.0, inset);
    const double safeThickness = std::max(0.0, thickness);
    const double x = bounds.x + safeInset;
    const double y = bounds.y + safeInset;
    const double width = std::max(0.0, bounds.width - safeInset * 2.0);
    const double height = std::max(0.0, bounds.height - safeInset * 2.0);
    const double edge = std::min({safeThickness, width, height});
    const double verticalHeight = std::max(0.0, height - edge * 2.0);
    return PaneDropBorder{
        .top = {x, y, width, edge},
        .right = {x + std::max(0.0, width - edge), y + edge, edge, verticalHeight},
        .bottom = {x, y + std::max(0.0, height - edge), width, edge},
        .left = {x, y + edge, edge, verticalHeight},
    };
}

Rect localSplitterDragPreview(const Rect boundary,
                              const Point pointer,
                              const bool verticalBoundary,
                              const double thickness,
                              const double maximumLength) noexcept {
    static_cast<void>(pointer);
    static_cast<void>(maximumLength);
    const double safeThickness = std::max(0.0, thickness);
    if (verticalBoundary) {
        const double width = std::min(safeThickness, std::max(0.0, boundary.width));
        const double x = boundary.x + std::max(0.0, (boundary.width - width) * 0.5);
        return {x, boundary.y, width, std::max(0.0, boundary.height)};
    }

    const double height = std::min(safeThickness, std::max(0.0, boundary.height));
    const double y = boundary.y + std::max(0.0, (boundary.height - height) * 0.5);
    return {boundary.x, y, std::max(0.0, boundary.width), height};
}

}
