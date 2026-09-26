#pragma once

#include "notepadFasaFiso/app/PresentationModel.hpp"
#include "notepadFasaFiso/gui/GuiGeometry.hpp"

#include <optional>
#include <span>
#include <vector>

namespace nff::gui {

struct ShellLayoutMetrics final {
    double outerFrameInset{0.0};
    double titleBarHeight{30.0};
    double topBarHeight{30.0};
    double sidebarSearchHeight{36.0};
    double tabStripHeight{34.0};
    double externalNoticeHeight{34.0};
    double statusBarHeight{24.0};
    double splitterThickness{4.0};
    double minimumPaneWidth{120.0};
    double minimumPaneHeight{80.0};

    [[nodiscard]] bool valid() const noexcept;
};

struct PaneLayout final {
    workspace::PaneId pane{};
    Rect bounds{};
    Rect tabStrip{};
    Rect content{};
    bool active{false};
};

struct SplitterLayout final {
    workspace::SplitId split{};
    workspace::SplitOrientation orientation{workspace::SplitOrientation::Horizontal};
    Rect bounds{};
    Rect track{};
};

struct ShellLayout final {
    Rect window{};
    Rect contentFrame{};
    Rect titleBar{};
    Rect topBar{};
    Rect sidebarSearch{};
    Rect sidebarBody{};
    Rect sidebarSplitter{};
    Rect externalNotice{};
    Rect workspace{};
    Rect statusBar{};
    std::vector<PaneLayout> panes;
    std::vector<SplitterLayout> splitters;
};

enum class HitRegion : unsigned char {
    None,
    FrameBorder,
    TitleBar,
    TopBar,
    SidebarSearch,
    SidebarBody,
    SidebarSplitter,
    PaneTabStrip,
    PaneContent,
    ExternalNotice,
    WorkspaceSplitter,
    StatusBar,
};

struct HitTarget final {
    HitRegion region{HitRegion::None};
    workspace::PaneId pane{};
    workspace::SplitId split{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return region != HitRegion::None;
    }
};

class ShellLayoutEngine final {
public:
    [[nodiscard]] static ShellLayout compute(
        const app::ApplicationPresentationSnapshot& presentation,
        Size viewport,
        const ShellLayoutMetrics& metrics = {});

    [[nodiscard]] static HitTarget hitTest(const ShellLayout& layout,
                                           Point point) noexcept;

    [[nodiscard]] static std::optional<double> snappedSplitterRatio(
        const SplitterLayout& dragged,
        double candidateRatio,
        std::span<const SplitterLayout> splitters,
        const ShellLayoutMetrics& metrics,
        double threshold = 10.0) noexcept;
};

}
