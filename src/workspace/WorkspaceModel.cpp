#include "notepadFasaFiso/workspace/WorkspaceModel.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace nff::workspace {

WorkspaceNode::Kind WorkspaceNode::kind() const noexcept {
    return kind_;
}

PaneId WorkspaceNode::pane() const noexcept {
    return pane_;
}

SplitId WorkspaceNode::split() const noexcept {
    return split_;
}

SplitOrientation WorkspaceNode::orientation() const noexcept {
    return orientation_;
}

double WorkspaceNode::ratio() const noexcept {
    return ratio_;
}

const WorkspaceNode* WorkspaceNode::first() const noexcept {
    return first_.get();
}

const WorkspaceNode* WorkspaceNode::second() const noexcept {
    return second_.get();
}

std::unique_ptr<WorkspaceNode> WorkspaceNode::makePane(const PaneId pane) {
    auto node = std::make_unique<WorkspaceNode>();
    node->kind_ = Kind::Pane;
    node->pane_ = pane;
    return node;
}

std::unique_ptr<WorkspaceNode> WorkspaceNode::makeSplit(
    const SplitId split,
    const SplitOrientation orientation,
    const double ratio,
    std::unique_ptr<WorkspaceNode> first,
    std::unique_ptr<WorkspaceNode> second) {
    auto node = std::make_unique<WorkspaceNode>();
    node->kind_ = Kind::Split;
    node->split_ = split;
    node->orientation_ = orientation;
    node->ratio_ = ratio;
    node->first_ = std::move(first);
    node->second_ = std::move(second);
    return node;
}

WorkspaceModel::WorkspaceModel() {
    primaryPane_ = allocatePaneId();
    activePane_ = primaryPane_;
    panes_.emplace(primaryPane_, PaneState{primaryPane_, {}, std::nullopt});
    root_ = WorkspaceNode::makePane(primaryPane_);
}

PaneId WorkspaceModel::primaryPane() const noexcept {
    return primaryPane_;
}

PaneId WorkspaceModel::activePane() const noexcept {
    return activePane_;
}

const WorkspaceNode& WorkspaceModel::root() const noexcept {
    return *root_;
}

const PaneState* WorkspaceModel::pane(const PaneId id) const noexcept {
    const auto iterator = panes_.find(id);
    return iterator == panes_.end() ? nullptr : &iterator->second;
}

const ViewState* WorkspaceModel::view(const ViewId id) const noexcept {
    const auto iterator = views_.find(id);
    return iterator == views_.end() ? nullptr : &iterator->second;
}

ViewState* WorkspaceModel::view(const ViewId id) noexcept {
    const auto iterator = views_.find(id);
    return iterator == views_.end() ? nullptr : &iterator->second;
}

std::optional<PaneId> WorkspaceModel::paneContaining(const ViewId viewId) const noexcept {
    const auto owner = viewOwners_.find(viewId.value);
    return owner == viewOwners_.end() ? std::nullopt
                                      : std::optional<PaneId>{owner->second};
}

ViewId WorkspaceModel::openView(const core::DocumentId document, const PaneId paneId) {
    if (!document) {
        return {};
    }

    auto paneIterator = panes_.find(paneId);
    if (paneIterator == panes_.end()) {
        return {};
    }

    if (nextViewId_ == std::numeric_limits<std::uint64_t>::max()) return {};
    const auto viewId = allocateViewId();
    ViewState view;
    view.id = viewId;
    view.document = document;
    const auto inserted = views_.emplace(viewId, std::move(view)).first;
    try {
        viewOwners_.emplace(viewId.value, paneId);
        try {
            paneIterator->second.views.push_back(viewId);
        } catch (...) {
            viewOwners_.erase(viewId.value);
            throw;
        }
    } catch (...) {
        views_.erase(inserted);
        throw;
    }
    paneIterator->second.activeView = viewId;
    activePane_ = paneId;
    return viewId;
}

bool WorkspaceModel::closeView(const ViewId viewId) noexcept {
    const auto viewIterator = views_.find(viewId);
    const auto owner = viewOwners_.find(viewId.value);
    if (viewIterator == views_.end() || owner == viewOwners_.end()) {
        return false;
    }

    const auto paneIterator = panes_.find(owner->second);
    if (paneIterator == panes_.end()) {
        return false;
    }
    eraseViewFromPane(paneIterator->second, viewId);
    viewOwners_.erase(owner);
    views_.erase(viewIterator);
    return true;
}

bool WorkspaceModel::moveView(const ViewId viewId,
                              const PaneId targetPaneId,
                              const std::optional<std::size_t> targetIndex) {
    if (views_.find(viewId) == views_.end()) {
        return false;
    }

    const auto sourcePaneId = paneContaining(viewId);
    if (!sourcePaneId) {
        return false;
    }

    auto sourceIterator = panes_.find(*sourcePaneId);
    auto targetIterator = panes_.find(targetPaneId);
    if (sourceIterator == panes_.end() || targetIterator == panes_.end()) {
        return false;
    }

    auto& source = sourceIterator->second;
    const auto sourceView = std::find(source.views.begin(), source.views.end(), viewId);
    if (sourceView == source.views.end()) {
        return false;
    }
    const auto sourceIndex = static_cast<std::size_t>(
        std::distance(source.views.begin(), sourceView));

    const bool samePane = *sourcePaneId == targetPaneId;
    if (samePane && !targetIndex) {
        return false;
    }

    const bool sourceWasActive = source.activeView == viewId;
    const auto targetSizeAfterRemoval =
        targetIterator->second.views.size() - (samePane ? 1U : 0U);
    const auto insertionIndex =
        std::min(targetIndex.value_or(targetSizeAfterRemoval), targetSizeAfterRemoval);

    if (samePane && insertionIndex == sourceIndex) {
        return false;
    }

    auto& target = targetIterator->second;
    if (!samePane) {
        target.views.insert(target.views.begin() + static_cast<std::ptrdiff_t>(insertionIndex),
                            viewId);
    }
    eraseViewFromPane(source, viewId);
    if (!samePane && sourceWasActive && !source.views.empty()) {
        const auto fallbackIndex = std::min(sourceIndex, source.views.size() - 1U);
        source.activeView = source.views[fallbackIndex];
    }

    if (samePane) {
        target.views.insert(target.views.begin() + static_cast<std::ptrdiff_t>(insertionIndex),
                            viewId);
    }
    target.activeView = viewId;
    if (!samePane) {
        viewOwners_.find(viewId.value)->second = targetPaneId;
    }
    activePane_ = targetPaneId;
    return true;
}

bool WorkspaceModel::setActiveView(const PaneId paneId, const ViewId viewId) noexcept {
    auto paneIterator = panes_.find(paneId);
    const auto owner = viewOwners_.find(viewId.value);
    if (paneIterator == panes_.end() || owner == viewOwners_.end() ||
        owner->second != paneId) {
        return false;
    }

    paneIterator->second.activeView = viewId;
    activePane_ = paneId;
    return true;
}

bool WorkspaceModel::setActivePane(const PaneId paneId) noexcept {
    if (panes_.find(paneId) == panes_.end()) {
        return false;
    }
    activePane_ = paneId;
    return true;
}

SplitResult WorkspaceModel::splitPane(const PaneId paneId,
                                      const SplitOrientation orientation,
                                      const SplitPlacement placement,
                                      const double ratio) {
    if (!validRatio(ratio) || panes_.find(paneId) == panes_.end()) {
        return {};
    }

    auto* paneNode = findPaneNode(root_.get(), paneId);
    if (paneNode == nullptr) {
        return {};
    }

    if (nextPaneId_ == std::numeric_limits<std::uint64_t>::max() ||
        nextSplitId_ == std::numeric_limits<std::uint64_t>::max()) return {};
    const auto newPane = allocatePaneId();
    const auto newSplit = allocateSplitId();

    auto existing = WorkspaceNode::makePane(paneId);
    auto created = WorkspaceNode::makePane(newPane);
    std::unique_ptr<WorkspaceNode> first;
    std::unique_ptr<WorkspaceNode> second;

    if (placement == SplitPlacement::Before) {
        first = std::move(created);
        second = std::move(existing);
    } else {
        first = std::move(existing);
        second = std::move(created);
    }

    auto splitNode = WorkspaceNode::makeSplit(newSplit, orientation, ratio,
                                              std::move(first), std::move(second));
    panes_.emplace(newPane, PaneState{newPane, {}, std::nullopt});
    *paneNode = std::move(*splitNode);
    return {newPane, newSplit};
}

bool WorkspaceModel::normalizeAlignedTwoByTwoRows(const double alignmentTolerance) noexcept {
    if (!root_ || !std::isfinite(alignmentTolerance) || alignmentTolerance < 0.0 ||
        root_->kind_ != WorkspaceNode::Kind::Split ||
        root_->orientation_ != SplitOrientation::Horizontal || !root_->first_ ||
        !root_->second_) {
        return false;
    }

    auto* leftColumn = root_->first_.get();
    auto* rightColumn = root_->second_.get();
    const auto isVerticalPair = [](const WorkspaceNode* node) noexcept {
        return node != nullptr && node->kind_ == WorkspaceNode::Kind::Split &&
               node->orientation_ == SplitOrientation::Vertical && node->first_ &&
               node->second_ && node->first_->kind_ == WorkspaceNode::Kind::Pane &&
               node->second_->kind_ == WorkspaceNode::Kind::Pane;
    };
    if (!isVerticalPair(leftColumn) || !isVerticalPair(rightColumn) ||
        std::abs(leftColumn->ratio_ - rightColumn->ratio_) > alignmentTolerance) {
        return false;
    }

    const auto horizontalSplit = root_->split_;
    const auto topBottomSplit = leftColumn->split_;
    const auto lowerHorizontalSplit = rightColumn->split_;
    const double horizontalRatio = root_->ratio_;
    const double rowRatio = (leftColumn->ratio_ + rightColumn->ratio_) * 0.5;

    // terry davis type shi'
    auto topLeft = std::move(leftColumn->first_);
    auto bottomLeft = std::move(leftColumn->second_);
    auto topRight = std::move(rightColumn->first_);
    auto bottomRight = std::move(rightColumn->second_);
    auto topRow = std::move(root_->first_);
    auto bottomRow = std::move(root_->second_);

    topRow->split_ = horizontalSplit;
    topRow->orientation_ = SplitOrientation::Horizontal;
    topRow->ratio_ = horizontalRatio;
    topRow->first_ = std::move(topLeft);
    topRow->second_ = std::move(topRight);

    bottomRow->split_ = lowerHorizontalSplit;
    bottomRow->orientation_ = SplitOrientation::Horizontal;
    bottomRow->ratio_ = horizontalRatio;
    bottomRow->first_ = std::move(bottomLeft);
    bottomRow->second_ = std::move(bottomRight);

    root_->split_ = topBottomSplit;
    root_->orientation_ = SplitOrientation::Vertical;
    root_->ratio_ = rowRatio;
    root_->first_ = std::move(topRow);
    root_->second_ = std::move(bottomRow);
    return true;
}

bool WorkspaceModel::normalizeAlignedTwoByTwoColumns(const double alignmentTolerance) noexcept {
    if (!root_ || !std::isfinite(alignmentTolerance) || alignmentTolerance < 0.0 ||
        root_->kind_ != WorkspaceNode::Kind::Split ||
        root_->orientation_ != SplitOrientation::Vertical || !root_->first_ ||
        !root_->second_) {
        return false;
    }

    auto* topRow = root_->first_.get();
    auto* bottomRow = root_->second_.get();
    const auto isHorizontalPair = [](const WorkspaceNode* node) noexcept {
        return node != nullptr && node->kind_ == WorkspaceNode::Kind::Split &&
               node->orientation_ == SplitOrientation::Horizontal && node->first_ &&
               node->second_ && node->first_->kind_ == WorkspaceNode::Kind::Pane &&
               node->second_->kind_ == WorkspaceNode::Kind::Pane;
    };
    if (!isHorizontalPair(topRow) || !isHorizontalPair(bottomRow) ||
        std::abs(topRow->ratio_ - bottomRow->ratio_) > alignmentTolerance) {
        return false;
    }

    const auto verticalSplit = root_->split_;
    const auto horizontalSplit = topRow->split_;
    const auto rightVerticalSplit = bottomRow->split_;
    const double rowRatio = root_->ratio_;
    const double columnRatio = (topRow->ratio_ + bottomRow->ratio_) * 0.5;

    auto topLeft = std::move(topRow->first_);
    auto topRight = std::move(topRow->second_);
    auto bottomLeft = std::move(bottomRow->first_);
    auto bottomRight = std::move(bottomRow->second_);
    auto leftColumn = std::move(root_->first_);
    auto rightColumn = std::move(root_->second_);

    leftColumn->split_ = verticalSplit;
    leftColumn->orientation_ = SplitOrientation::Vertical;
    leftColumn->ratio_ = rowRatio;
    leftColumn->first_ = std::move(topLeft);
    leftColumn->second_ = std::move(bottomLeft);

    rightColumn->split_ = rightVerticalSplit;
    rightColumn->orientation_ = SplitOrientation::Vertical;
    rightColumn->ratio_ = rowRatio;
    rightColumn->first_ = std::move(topRight);
    rightColumn->second_ = std::move(bottomRight);

    root_->split_ = horizontalSplit;
    root_->orientation_ = SplitOrientation::Horizontal;
    root_->ratio_ = columnRatio;
    root_->first_ = std::move(leftColumn);
    root_->second_ = std::move(rightColumn);
    return true;
}

bool WorkspaceModel::setSplitRatio(const SplitId splitId, const double ratio) noexcept {
    if (!validRatio(ratio)) {
        return false;
    }

    auto* node = findSplitNode(root_.get(), splitId);
    if (node == nullptr) {
        return false;
    }
    node->ratio_ = ratio;
    return true;
}

bool WorkspaceModel::removeEmptyPane(const PaneId paneId) noexcept {
    if (panes_.size() <= 1) {
        return false;
    }

    const auto iterator = panes_.find(paneId);
    if (iterator == panes_.end() || !iterator->second.views.empty()) {
        return false;
    }

    if (!removePaneNode(root_, paneId)) {
        return false;
    }

    panes_.erase(iterator);
    if (primaryPane_ == paneId) {
        primaryPane_ = panes_.begin()->first;
    }
    if (activePane_ == paneId) {
        activePane_ = primaryPane_;
    }
    return true;
}

std::size_t WorkspaceModel::paneCount() const noexcept {
    return panes_.size();
}

std::size_t WorkspaceModel::viewCount() const noexcept {
    return views_.size();
}

WorkspaceSnapshot WorkspaceModel::snapshot() const {
    WorkspaceSnapshot result;
    result.primaryPane = primaryPane_;
    result.activePane = activePane_;
    result.nextPaneId = nextPaneId_;
    result.nextViewId = nextViewId_;
    result.nextSplitId = nextSplitId_;
    result.root = snapshotNode(root_.get());
    result.panes.reserve(panes_.size());
    for (const auto& [id, paneState] : panes_) {
        static_cast<void>(id);
        result.panes.push_back(paneState);
    }
    result.views.reserve(views_.size());
    for (const auto& [id, viewState] : views_) {
        static_cast<void>(id);
        result.views.push_back(viewState);
    }
    return result;
}

bool WorkspaceModel::restore(const WorkspaceSnapshot& source) {
    if (!validateSnapshot(source)) {
        return false;
    }

    std::map<PaneId, PaneState> panes;
    for (const auto& paneState : source.panes) {
        panes.emplace(paneState.id, paneState);
    }
    std::map<ViewId, ViewState> views;
    for (const auto& viewState : source.views) {
        views.emplace(viewState.id, viewState);
    }
    std::unordered_map<std::uint64_t, PaneId> viewOwners;
    viewOwners.reserve(source.views.size());
    for (const auto& paneState : source.panes) {
        for (const auto viewId : paneState.views) {
            viewOwners.emplace(viewId.value, paneState.id);
        }
    }
    auto root = restoreNode(source.root.get());
    if (!root) {
        return false;
    }

    panes_ = std::move(panes);
    views_ = std::move(views);
    viewOwners_ = std::move(viewOwners);
    root_ = std::move(root);
    primaryPane_ = source.primaryPane;
    activePane_ = source.activePane;
    nextPaneId_ = source.nextPaneId;
    nextViewId_ = source.nextViewId;
    nextSplitId_ = source.nextSplitId;
    return true;
}

PaneId WorkspaceModel::allocatePaneId() noexcept {
    return PaneId{nextPaneId_++};
}

ViewId WorkspaceModel::allocateViewId() noexcept {
    return ViewId{nextViewId_++};
}

SplitId WorkspaceModel::allocateSplitId() noexcept {
    return SplitId{nextSplitId_++};
}

WorkspaceNode* WorkspaceModel::findPaneNode(WorkspaceNode* node, const PaneId paneId) noexcept {
    if (node == nullptr) {
        return nullptr;
    }
    if (node->kind_ == WorkspaceNode::Kind::Pane) {
        return node->pane_ == paneId ? node : nullptr;
    }
    if (auto* first = findPaneNode(node->first_.get(), paneId)) {
        return first;
    }
    return findPaneNode(node->second_.get(), paneId);
}

WorkspaceNode* WorkspaceModel::findSplitNode(WorkspaceNode* node,
                                             const SplitId splitId) noexcept {
    if (node == nullptr) {
        return nullptr;
    }
    if (node->kind_ == WorkspaceNode::Kind::Split && node->split_ == splitId) {
        return node;
    }
    if (auto* first = findSplitNode(node->first_.get(), splitId)) {
        return first;
    }
    return findSplitNode(node->second_.get(), splitId);
}

bool WorkspaceModel::removePaneNode(std::unique_ptr<WorkspaceNode>& node,
                                    const PaneId paneId) noexcept {
    if (!node || node->kind_ != WorkspaceNode::Kind::Split) {
        return false;
    }

    if (node->first_ && node->first_->kind_ == WorkspaceNode::Kind::Pane &&
        node->first_->pane_ == paneId) {
        node = std::move(node->second_);
        return true;
    }
    if (node->second_ && node->second_->kind_ == WorkspaceNode::Kind::Pane &&
        node->second_->pane_ == paneId) {
        node = std::move(node->first_);
        return true;
    }

    return removePaneNode(node->first_, paneId) || removePaneNode(node->second_, paneId);
}

bool WorkspaceModel::validRatio(const double ratio) noexcept {
    return ratio >= minimumSplitRatio && ratio <= maximumSplitRatio;
}

void WorkspaceModel::eraseViewFromPane(PaneState& paneState, const ViewId viewId) noexcept {
    const auto iterator = std::find(paneState.views.begin(), paneState.views.end(), viewId);
    if (iterator == paneState.views.end()) {
        return;
    }

    const bool wasActive = paneState.activeView == viewId;
    const auto removedIndex = static_cast<std::size_t>(
        std::distance(paneState.views.begin(), iterator));
    paneState.views.erase(iterator);
    if (wasActive) {
        if (paneState.views.empty()) {
            paneState.activeView.reset();
        } else {
            const auto replacementIndex = std::min(removedIndex, paneState.views.size() - 1U);
            paneState.activeView = paneState.views[replacementIndex];
        }
    }
}

std::unique_ptr<WorkspaceSnapshotNode> WorkspaceModel::snapshotNode(const WorkspaceNode* node) {
    if (node == nullptr) {
        return {};
    }
    auto result = std::make_unique<WorkspaceSnapshotNode>();
    result->kind = node->kind_;
    result->pane = node->pane_;
    result->split = node->split_;
    result->orientation = node->orientation_;
    result->ratio = node->ratio_;
    result->first = snapshotNode(node->first_.get());
    result->second = snapshotNode(node->second_.get());
    return result;
}

std::unique_ptr<WorkspaceNode> WorkspaceModel::restoreNode(const WorkspaceSnapshotNode* node) {
    if (node == nullptr) {
        return {};
    }
    if (node->kind == WorkspaceNode::Kind::Pane) {
        return WorkspaceNode::makePane(node->pane);
    }
    auto first = restoreNode(node->first.get());
    auto second = restoreNode(node->second.get());
    if (!first || !second) {
        return {};
    }
    return WorkspaceNode::makeSplit(node->split, node->orientation, node->ratio,
                                    std::move(first), std::move(second));
}

bool WorkspaceModel::validateSnapshot(const WorkspaceSnapshot& snapshot) {
    if (!snapshot.primaryPane || !snapshot.activePane || !snapshot.root ||
        snapshot.nextPaneId == 0U || snapshot.nextViewId == 0U || snapshot.nextSplitId == 0U) {
        return false;
    }

    std::set<PaneId> paneIds;
    std::set<ViewId> viewIds;
    std::set<ViewId> assignedViews;
    std::uint64_t maximumPane = 0;
    std::uint64_t maximumView = 0;
    std::uint64_t maximumSplit = 0;

    for (const auto& paneState : snapshot.panes) {
        if (!paneState.id || !paneIds.insert(paneState.id).second) {
            return false;
        }
        maximumPane = std::max(maximumPane, paneState.id.value);
    }
    for (const auto& viewState : snapshot.views) {
        if (!viewState.id || !viewState.document || !viewIds.insert(viewState.id).second ||
            (viewState.fontFamilyOverride && viewState.fontFamilyOverride->size() > 16U * 1024U) ||
            (viewState.fontPointSizeOverride &&
             (!std::isfinite(*viewState.fontPointSizeOverride) ||
              *viewState.fontPointSizeOverride < 4.0 ||
              *viewState.fontPointSizeOverride > 256.0))) {
            return false;
        }
        maximumView = std::max(maximumView, viewState.id.value);
    }
    if (!paneIds.contains(snapshot.primaryPane) || !paneIds.contains(snapshot.activePane)) {
        return false;
    }

    for (const auto& paneState : snapshot.panes) {
        for (const auto viewId : paneState.views) {
            if (!viewIds.contains(viewId) || !assignedViews.insert(viewId).second) {
                return false;
            }
        }
        if (paneState.activeView &&
            std::find(paneState.views.begin(), paneState.views.end(), *paneState.activeView) ==
                paneState.views.end()) {
            return false;
        }
    }
    if (assignedViews.size() != viewIds.size()) {
        return false;
    }

    std::set<PaneId> layoutPanes;
    std::set<SplitId> splitIds;
    const auto walk = [&](const auto& self, const WorkspaceSnapshotNode* node) -> bool {
        if (node == nullptr) {
            return false;
        }
        if (node->kind == WorkspaceNode::Kind::Pane) {
            return node->pane && !node->first && !node->second && paneIds.contains(node->pane) &&
                   layoutPanes.insert(node->pane).second;
        }
        if (!node->split || !validRatio(node->ratio) || !std::isfinite(node->ratio) ||
            !node->first || !node->second || !splitIds.insert(node->split).second) {
            return false;
        }
        maximumSplit = std::max(maximumSplit, node->split.value);
        return self(self, node->first.get()) && self(self, node->second.get());
    };
    if (!walk(walk, snapshot.root.get()) || layoutPanes != paneIds) {
        return false;
    }

    return snapshot.nextPaneId > maximumPane && snapshot.nextViewId > maximumView &&
           snapshot.nextSplitId > maximumSplit;
}

}
