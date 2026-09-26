#pragma once

#include "notepadFasaFiso/app/PresentationModel.hpp"
#include "notepadFasaFiso/gui/ShellLayout.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace nff::gui {

struct GuiFrame final {
    std::uint64_t generation{0U};
    app::ApplicationPresentationSnapshot presentation;
    ShellLayout layout;
};

class GuiShell final {
public:
    GuiShell(workspace::WorkspaceModel& workspace,
             app::PresentationModel& presentation) noexcept;

    void setViewport(Size viewport) noexcept;
    [[nodiscard]] Size viewport() const noexcept;

    [[nodiscard]] bool setLayoutMetrics(const ShellLayoutMetrics& metrics) noexcept;
    [[nodiscard]] const ShellLayoutMetrics& layoutMetrics() const noexcept;

    void setSidebarVisible(bool visible) noexcept;
    void setSidebarWidth(double width) noexcept;

    [[nodiscard]] bool activatePane(workspace::PaneId pane) noexcept;
    [[nodiscard]] bool activateView(workspace::ViewId view) noexcept;
    [[nodiscard]] workspace::ViewId openDocumentInPane(core::DocumentId document,
                                                       workspace::PaneId pane);
    [[nodiscard]] workspace::SplitResult splitPane(
        workspace::PaneId pane,
        workspace::SplitOrientation orientation,
        workspace::SplitPlacement placement = workspace::SplitPlacement::After,
        double ratio = 0.5);
    [[nodiscard]] workspace::ViewId duplicateView(workspace::ViewId source,
                                                  workspace::PaneId targetPane);
    [[nodiscard]] bool setSplitRatio(workspace::SplitId split, double ratio) noexcept;
    [[nodiscard]] bool normalizeAlignedTwoByTwoRows() noexcept;
    [[nodiscard]] bool normalizeAlignedTwoByTwoColumns() noexcept;
    [[nodiscard]] bool moveView(workspace::ViewId view,
                                workspace::PaneId pane,
                                std::optional<std::size_t> targetIndex = std::nullopt);
    [[nodiscard]] bool closeView(workspace::ViewId view) noexcept;
    [[nodiscard]] std::vector<workspace::ViewId> closePane(workspace::PaneId pane);

    void requestRefresh() noexcept;
    [[nodiscard]] const GuiFrame& refresh();
    [[nodiscard]] const GuiFrame& frame() const noexcept;
    [[nodiscard]] HitTarget hitTest(Point point) const noexcept;

private:
    void invalidate() noexcept;

    workspace::WorkspaceModel* workspace_{};
    app::PresentationModel* presentation_{};
    Size viewport_{1280.0, 720.0};
    ShellLayoutMetrics metrics_{};
    GuiFrame frame_{};
    std::uint64_t generation_{0U};
    bool dirty_{true};
};

}
