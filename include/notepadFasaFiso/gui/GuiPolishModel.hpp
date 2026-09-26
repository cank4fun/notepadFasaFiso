#pragma once

#include "notepadFasaFiso/gui/GuiGeometry.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace nff::gui {

struct PaneDropBorder final {
    Rect top{};
    Rect right{};
    Rect bottom{};
    Rect left{};
};

struct LinuxChromeMetrics final {
    int menuPaddingX{12};
    int menuGap{4};
    friend bool operator==(const LinuxChromeMetrics&, const LinuxChromeMetrics&) = default;
};

enum class ConfirmationButtonRole : unsigned char {
    Cancel,
    Proceed,
};

struct TabTopologyEntry final {
    std::uint64_t pane{0U};
    std::uint64_t view{0U};
    friend bool operator==(const TabTopologyEntry&, const TabTopologyEntry&) = default;
};

class TabTopologyHasher final {
public:
    explicit TabTopologyHasher(bool tabsVisible) noexcept;
    void add(std::uint64_t pane, std::uint64_t view) noexcept;
    [[nodiscard]] std::uint64_t value() const noexcept;

private:
    void mix(std::uint64_t value) noexcept;
    std::uint64_t hash_{1469598103934665603ULL};
};

[[nodiscard]] std::uint64_t tabTopologyFingerprint(
    bool tabsVisible,
    std::span<const TabTopologyEntry> entries) noexcept;

struct PixelBounds final {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
    [[nodiscard]] bool operator==(const PixelBounds&) const noexcept = default;
};

template <typename T>
class NativeChangeLatch final {
public:
    [[nodiscard]] bool update(const T& value) {
        if (value_ && *value_ == value) {
            return false;
        }
        value_ = value;
        return true;
    }
    void invalidate() noexcept { value_.reset(); }
private:
    std::optional<T> value_;
};

class PointerDragLatch final {
public:
    void begin() noexcept;
    void end() noexcept;
    void cancel() noexcept;
    [[nodiscard]] bool active() const noexcept;

private:
    bool active_{false};
};

class PointerPressLatch final {
public:
    void press(bool insideTarget) noexcept;
    [[nodiscard]] bool release(bool insideTarget) noexcept;
    void cancel() noexcept;
    [[nodiscard]] bool armed() const noexcept;

private:
    bool armed_{false};
};

class TextColorStyleRefreshLatch final {
public:
    void invalidateAppearance() noexcept;
    void markRendered() noexcept;
    [[nodiscard]] bool needsRefresh() const noexcept;

private:
    bool needsRefresh_{true};
};

[[nodiscard]] constexpr ConfirmationButtonRole confirmationDefaultButton() noexcept {
    return ConfirmationButtonRole::Cancel;
}

[[nodiscard]] constexpr LinuxChromeMetrics linuxChromeMetrics() noexcept { return {}; }

[[nodiscard]] PixelBounds pixelBounds(Rect bounds) noexcept;
[[nodiscard]] std::string formatPointSizeForUi(double pointSize);
[[nodiscard]] std::string statusValueForUi(std::string_view label,
                                           std::string_view value);
[[nodiscard]] PaneDropBorder paneDropBorder(Rect bounds,
                                            double thickness,
                                            double inset) noexcept;
[[nodiscard]] Rect localSplitterDragPreview(Rect boundary,
                                            Point pointer,
                                            bool verticalBoundary,
                                            double thickness,
                                            double maximumLength) noexcept;

}
