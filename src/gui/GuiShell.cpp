#include "notepadFasaFiso/gui/GuiShell.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace nff::gui {

GuiShell::GuiShell(workspace::WorkspaceModel& workspace,
                   app::PresentationModel& presentation) noexcept
    : workspace_(&workspace), presentation_(&presentation) {}

void GuiShell::setViewport(Size viewport) noexcept {
    if (!finite(viewport)) {
        return;
    }
    viewport.width = std::max(0.0, viewport.width);
    viewport.height = std::max(0.0, viewport.height);
    if (viewport_.width == viewport.width && viewport_.height == viewport.height) {
        return;
    }
    viewport_ = viewport;
    invalidate();
}

Size GuiShell::viewport() const noexcept {
    return viewport_;
}

bool GuiShell::setLayoutMetrics(const ShellLayoutMetrics& metrics) noexcept {
    if (!metrics.valid()) {
        return false;
    }
    metrics_ = metrics;
    invalidate();
    return true;
}

const ShellLayoutMetrics& GuiShell::layoutMetrics() const noexcept {
    return metrics_;
}

void GuiShell::setSidebarVisible(const bool visible) noexcept {
    presentation_->setSidebarVisible(visible);
    invalidate();
}

void GuiShell::setSidebarWidth(const double width) noexcept {
    presentation_->setSidebarWidth(width);
    invalidate();
}

bool GuiShell::activatePane(const workspace::PaneId pane) noexcept {
    if (!workspace_->setActivePane(pane)) {
        return false;
    }
    invalidate();
    return true;
}

bool GuiShell::activateView(const workspace::ViewId view) noexcept {
    const auto pane = workspace_->paneContaining(view);
    if (!pane || !workspace_->setActiveView(*pane, view)) {
        return false;
    }
    invalidate();
    return true;
}

workspace::ViewId GuiShell::openDocumentInPane(const core::DocumentId document,
                                               const workspace::PaneId pane) {
    const auto* paneState = workspace_->pane(pane);
    if (!document || paneState == nullptr) {
        return {};
    }

    for (const auto view : paneState->views) {
        const auto* state = workspace_->view(view);
        if (state != nullptr && state->document == document) {
            if (workspace_->setActiveView(pane, view)) {
                invalidate();
                return view;
            }
            return {};
        }
    }

    const auto view = workspace_->openView(document, pane);
    if (view) {
        invalidate();
    }
    return view;
}

workspace::SplitResult GuiShell::splitPane(
    const workspace::PaneId pane,
    const workspace::SplitOrientation orientation,
    const workspace::SplitPlacement placement,
    const double ratio) {
    const auto result = workspace_->splitPane(pane, orientation, placement, ratio);
    if (result) {
        static_cast<void>(workspace_->normalizeAlignedTwoByTwoRows());
        invalidate();
    }
    return result;
}

workspace::ViewId GuiShell::duplicateView(const workspace::ViewId source,
                                          const workspace::PaneId targetPane) {
    const auto* sourceState = workspace_->view(source);
    if (sourceState == nullptr) {
        return {};
    }

    const auto sourceCopy = *sourceState;
    const auto* sourceRuntime = presentation_->viewRuntime(source);
    const auto runtimeCopy = sourceRuntime != nullptr
                                 ? std::optional<app::ViewRuntimeState>{*sourceRuntime}
                                 : std::nullopt;

    const auto duplicate = workspace_->openView(sourceCopy.document, targetPane);
    if (!duplicate) {
        return {};
    }

    auto* duplicateState = workspace_->view(duplicate);
    if (duplicateState == nullptr) {
        static_cast<void>(workspace_->closeView(duplicate));
        return {};
    }

    const auto duplicateId = duplicateState->id;
    *duplicateState = sourceCopy;
    duplicateState->id = duplicateId;

    if (runtimeCopy) {
        static_cast<void>(presentation_->setViewRuntime(duplicate, *runtimeCopy));
    }

    invalidate();
    return duplicate;
}

bool GuiShell::setSplitRatio(const workspace::SplitId split,
                             const double ratio) noexcept {
    if (!workspace_->setSplitRatio(split, ratio)) {
        return false;
    }
    invalidate();
    return true;
}

bool GuiShell::normalizeAlignedTwoByTwoRows() noexcept {
    if (!workspace_->normalizeAlignedTwoByTwoRows()) {
        return false;
    }
    invalidate();
    return true;
}

bool GuiShell::normalizeAlignedTwoByTwoColumns() noexcept {
    if (!workspace_->normalizeAlignedTwoByTwoColumns()) {
        return false;
    }
    invalidate();
    return true;
}

bool GuiShell::moveView(const workspace::ViewId view,
                        const workspace::PaneId pane,
                        const std::optional<std::size_t> targetIndex) {
    if (!workspace_->moveView(view, pane, targetIndex)) {
        return false;
    }
    invalidate();
    return true;
}

bool GuiShell::closeView(const workspace::ViewId view) noexcept {
    const auto pane = workspace_->paneContaining(view);
    if (!workspace_->closeView(view)) {
        return false;
    }
    if (pane && *pane != workspace_->primaryPane()) {
        const auto* state = workspace_->pane(*pane);
        if (state != nullptr && state->views.empty()) {
            static_cast<void>(workspace_->removeEmptyPane(*pane));
        }
    }
    static_cast<void>(presentation_->eraseViewRuntime(view));
    invalidate();
    return true;
}

std::vector<workspace::ViewId> GuiShell::closePane(const workspace::PaneId pane) {
    const auto* state = workspace_->pane(pane);
    if (state == nullptr || workspace_->paneCount() <= 1U) {
        return {};
    }

    const auto views = state->views;
    std::vector<workspace::ViewId> closed;
    closed.reserve(views.size());
    for (const auto view : views) {
        if (!workspace_->closeView(view)) {
            continue;
        }
        static_cast<void>(presentation_->eraseViewRuntime(view));
        closed.push_back(view);
    }

    const auto* remaining = workspace_->pane(pane);
    if (remaining != nullptr && remaining->views.empty()) {
        static_cast<void>(workspace_->removeEmptyPane(pane));
    }
    invalidate();
    return closed;
}

void GuiShell::requestRefresh() noexcept {
    invalidate();
}

const GuiFrame& GuiShell::refresh() {
    if (!dirty_) {
        return frame_;
    }

    presentation_->sync();
    auto snapshot = presentation_->snapshot();
    auto layout = ShellLayoutEngine::compute(snapshot, viewport_, metrics_);
    ++generation_;
    frame_.generation = generation_;
    frame_.presentation = std::move(snapshot);
    frame_.layout = std::move(layout);
    dirty_ = false;
    return frame_;
}

const GuiFrame& GuiShell::frame() const noexcept {
    return frame_;
}

HitTarget GuiShell::hitTest(const Point point) const noexcept {
    return ShellLayoutEngine::hitTest(frame_.layout, point);
}

void GuiShell::invalidate() noexcept {
    dirty_ = true;
}

}
