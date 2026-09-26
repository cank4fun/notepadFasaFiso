#include "notepadFasaFiso/gui/ShellLayout.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace nff::gui {
namespace {

[[nodiscard]] double nonNegative(const double value) noexcept {
    return std::isfinite(value) ? std::max(value, 0.0) : 0.0;
}

[[nodiscard]] double boundedExtent(const double available,
                                   const double ratio,
                                   const double minimum) noexcept {
    if (available <= 0.0) {
        return 0.0;
    }
    if (available < minimum * 2.0) {
        return available * 0.5;
    }
    return std::clamp(available * ratio, minimum, available - minimum);
}

[[nodiscard]] bool paneIsActive(const app::ApplicationPresentationSnapshot& presentation,
                                const workspace::PaneId pane) noexcept {
    return pane == presentation.activePane;
}

void layoutNode(const app::SplitPresentationNode* node,
                const Rect bounds,
                const app::ApplicationPresentationSnapshot& presentation,
                const ShellLayoutMetrics& metrics,
                ShellLayout& output) {
    if (node == nullptr || bounds.empty()) {
        return;
    }

    if (node->kind == workspace::WorkspaceNode::Kind::Pane) {
        PaneLayout pane;
        pane.pane = node->pane;
        pane.bounds = bounds;
        pane.active = paneIsActive(presentation, node->pane);

        const double tabHeight = presentation.tabsVisible
                                     ? std::min(metrics.tabStripHeight, bounds.height)
                                     : 0.0;
        if (tabHeight > 0.0) {
            pane.tabStrip = {bounds.x, bounds.y, bounds.width, tabHeight};
        }
        pane.content = {
            bounds.x,
            bounds.y + tabHeight,
            bounds.width,
            std::max(0.0, bounds.height - tabHeight),
        };
        output.panes.push_back(pane);
        return;
    }

    if (node->first == nullptr || node->second == nullptr) {
        return;
    }

    const double thickness = metrics.splitterThickness;
    SplitterLayout splitter;
    splitter.split = node->split;
    splitter.orientation = node->orientation;
    splitter.track = bounds;

    if (node->orientation == workspace::SplitOrientation::Horizontal) {
        const double available = std::max(0.0, bounds.width - thickness);
        const double firstWidth = boundedExtent(
            available, node->ratio, metrics.minimumPaneWidth);
        const double secondWidth = std::max(0.0, available - firstWidth);
        const Rect firstBounds{bounds.x, bounds.y, firstWidth, bounds.height};
        splitter.bounds = {bounds.x + firstWidth, bounds.y, thickness, bounds.height};
        const Rect secondBounds{
            splitter.bounds.right(), bounds.y, secondWidth, bounds.height};

        output.splitters.push_back(splitter);
        layoutNode(node->first.get(), firstBounds, presentation, metrics, output);
        layoutNode(node->second.get(), secondBounds, presentation, metrics, output);
        return;
    }

    const double available = std::max(0.0, bounds.height - thickness);
    const double firstHeight = boundedExtent(
        available, node->ratio, metrics.minimumPaneHeight);
    const double secondHeight = std::max(0.0, available - firstHeight);
    const Rect firstBounds{bounds.x, bounds.y, bounds.width, firstHeight};
    splitter.bounds = {bounds.x, bounds.y + firstHeight, bounds.width, thickness};
    const Rect secondBounds{
        bounds.x, splitter.bounds.bottom(), bounds.width, secondHeight};

    output.splitters.push_back(splitter);
    layoutNode(node->first.get(), firstBounds, presentation, metrics, output);
    layoutNode(node->second.get(), secondBounds, presentation, metrics, output);
}

}

bool ShellLayoutMetrics::valid() const noexcept {
    return std::isfinite(outerFrameInset) && outerFrameInset >= 0.0 &&
           std::isfinite(titleBarHeight) && titleBarHeight >= 0.0 &&
           std::isfinite(topBarHeight) && topBarHeight >= 0.0 &&
           std::isfinite(sidebarSearchHeight) && sidebarSearchHeight >= 0.0 &&
           std::isfinite(tabStripHeight) && tabStripHeight >= 0.0 &&
           std::isfinite(externalNoticeHeight) && externalNoticeHeight >= 0.0 &&
           std::isfinite(statusBarHeight) && statusBarHeight >= 0.0 &&
           std::isfinite(splitterThickness) && splitterThickness >= 0.0 &&
           std::isfinite(minimumPaneWidth) && minimumPaneWidth >= 0.0 &&
           std::isfinite(minimumPaneHeight) && minimumPaneHeight >= 0.0;
}

ShellLayout ShellLayoutEngine::compute(
    const app::ApplicationPresentationSnapshot& presentation,
    Size viewport,
    const ShellLayoutMetrics& metrics) {
    ShellLayout result;
    if (!finite(viewport) || !metrics.valid()) {
        return result;
    }

    viewport.width = nonNegative(viewport.width);
    viewport.height = nonNegative(viewport.height);
    result.window = {0.0, 0.0, viewport.width, viewport.height};

    const double maximumInset = std::min(viewport.width, viewport.height) * 0.5;
    const double frameInset = std::min(metrics.outerFrameInset, maximumInset);
    result.contentFrame = {
        frameInset,
        frameInset,
        std::max(0.0, viewport.width - frameInset * 2.0),
        std::max(0.0, viewport.height - frameInset * 2.0),
    };

    const double titleHeight = std::min(metrics.titleBarHeight, result.contentFrame.height);
    result.titleBar = {result.contentFrame.x, result.contentFrame.y,
                       result.contentFrame.width, titleHeight};

    const double remainingAfterTitle = std::max(0.0, result.contentFrame.height - titleHeight);
    const double topHeight = std::min(metrics.topBarHeight, remainingAfterTitle);
    result.topBar = {result.contentFrame.x, result.contentFrame.y + titleHeight,
                     result.contentFrame.width, topHeight};

    const double bodyY = result.contentFrame.y + titleHeight + topHeight;
    const double bodyHeight = std::max(0.0, result.contentFrame.bottom() - bodyY);

    double workspaceX = result.contentFrame.x;
    double workspaceWidth = result.contentFrame.width;
    if (presentation.sidebar.visible && result.contentFrame.width > 0.0 && bodyHeight > 0.0) {
        const double maximumSidebar = std::max(
            0.0, result.contentFrame.width - metrics.minimumPaneWidth - metrics.splitterThickness);
        const double desiredSidebar = std::max(0.0, presentation.sidebar.width);
        const double sidebarWidth = std::min(desiredSidebar, maximumSidebar);
        const double separator = std::min(
            metrics.splitterThickness, std::max(0.0, result.contentFrame.width - sidebarWidth));
        const double searchHeight = std::min(metrics.sidebarSearchHeight, bodyHeight);

        result.sidebarSearch = {result.contentFrame.x, bodyY, sidebarWidth, searchHeight};
        result.sidebarBody = {
            result.contentFrame.x,
            bodyY + searchHeight,
            sidebarWidth,
            std::max(0.0, bodyHeight - searchHeight),
        };
        result.sidebarSplitter = {result.contentFrame.x + sidebarWidth, bodyY, separator, bodyHeight};
        workspaceX = result.sidebarSplitter.right();
        workspaceWidth = std::max(0.0, result.contentFrame.right() - workspaceX);
    }

    const double statusHeight = std::min(metrics.statusBarHeight, bodyHeight);
    const double contentHeight = std::max(0.0, bodyHeight - statusHeight);
    const double noticeHeight = (presentation.status.externalConflict ||
                                 presentation.status.recovered)
                                    ? std::min(metrics.externalNoticeHeight, contentHeight)
                                    : 0.0;
    if (noticeHeight > 0.0) {
        result.externalNotice = {workspaceX, bodyY, workspaceWidth, noticeHeight};
    }
    const double workspaceHeight = std::max(0.0, contentHeight - noticeHeight);
    result.workspace = {workspaceX, bodyY + noticeHeight, workspaceWidth, workspaceHeight};
    result.statusBar = {
        workspaceX,
        bodyY + contentHeight,
        workspaceWidth,
        statusHeight,
    };

    layoutNode(presentation.splitRoot.get(), result.workspace, presentation, metrics, result);
    return result;
}

std::optional<double> ShellLayoutEngine::snappedSplitterRatio(
    const SplitterLayout& dragged,
    const double candidateRatio,
    const std::span<const SplitterLayout> splitters,
    const ShellLayoutMetrics& metrics,
    const double threshold) noexcept {
    if (!std::isfinite(candidateRatio) || !std::isfinite(threshold) || threshold < 0.0 ||
        !metrics.valid()) {
        return std::nullopt;
    }

    const bool horizontal = dragged.orientation == workspace::SplitOrientation::Horizontal;
    const double thickness = metrics.splitterThickness;
    const double available = horizontal
                                 ? std::max(0.0, dragged.track.width - thickness)
                                 : std::max(0.0, dragged.track.height - thickness);
    const double minimum = horizontal ? metrics.minimumPaneWidth : metrics.minimumPaneHeight;
    if (available <= 0.0 || available < minimum * 2.0) {
        return std::nullopt;
    }

    const double clampedRatio = std::clamp(candidateRatio, minimum / available,
                                           1.0 - minimum / available);
    const double candidateCoordinate = (horizontal ? dragged.track.x : dragged.track.y) +
                                       available * clampedRatio;

    std::optional<double> bestRatio;
    double bestDistance = threshold + 1.0;
    for (const auto& other : splitters) {
        if (other.split == dragged.split || other.orientation != dragged.orientation) {
            continue;
        }
        const double boundaryCoordinate = horizontal ? other.bounds.x : other.bounds.y;
        const double extent = boundaryCoordinate - (horizontal ? dragged.track.x : dragged.track.y);
        if (!std::isfinite(boundaryCoordinate) || extent < minimum ||
            extent > available - minimum) {
            continue;
        }
        const double distance = std::abs(boundaryCoordinate - candidateCoordinate);
        if (distance <= threshold && distance < bestDistance) {
            bestDistance = distance;
            bestRatio = extent / available;
        }
    }
    return bestRatio;
}

HitTarget ShellLayoutEngine::hitTest(const ShellLayout& layout,
                                     const Point point) noexcept {
    if (!finite(point) || !layout.window.contains(point)) {
        return {};
    }
    if (!layout.contentFrame.contains(point)) {
        return {HitRegion::FrameBorder, {}, {}};
    }

    for (const auto& splitter : layout.splitters) {
        if (splitter.bounds.contains(point)) {
            return {HitRegion::WorkspaceSplitter, {}, splitter.split};
        }
    }
    if (layout.sidebarSplitter.contains(point)) {
        return {HitRegion::SidebarSplitter, {}, {}};
    }
    for (const auto& pane : layout.panes) {
        if (pane.tabStrip.contains(point)) {
            return {HitRegion::PaneTabStrip, pane.pane, {}};
        }
        if (pane.content.contains(point)) {
            return {HitRegion::PaneContent, pane.pane, {}};
        }
    }
    if (layout.externalNotice.contains(point)) {
        return {HitRegion::ExternalNotice, {}, {}};
    }
    if (layout.statusBar.contains(point)) {
        return {HitRegion::StatusBar, {}, {}};
    }
    if (layout.sidebarSearch.contains(point)) {
        return {HitRegion::SidebarSearch, {}, {}};
    }
    if (layout.sidebarBody.contains(point)) {
        return {HitRegion::SidebarBody, {}, {}};
    }
    if (layout.topBar.contains(point)) {
        return {HitRegion::TopBar, {}, {}};
    }
    if (layout.titleBar.contains(point)) {
        return {HitRegion::TitleBar, {}, {}};
    }
    return {};
}

}
