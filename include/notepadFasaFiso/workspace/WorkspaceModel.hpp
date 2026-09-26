#pragma once

#include "notepadFasaFiso/core/DocumentManager.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nff::workspace {

struct PaneId {
    std::uint64_t value{0};
    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
    friend bool operator==(PaneId, PaneId) = default;
    friend auto operator<=>(PaneId, PaneId) = default;
};

struct ViewId {
    std::uint64_t value{0};
    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
    friend bool operator==(ViewId, ViewId) = default;
    friend auto operator<=>(ViewId, ViewId) = default;
};

struct SplitId {
    std::uint64_t value{0};
    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
    friend bool operator==(SplitId, SplitId) = default;
    friend auto operator<=>(SplitId, SplitId) = default;
};

enum class SplitOrientation : std::uint8_t {
    Horizontal,
    Vertical
};

enum class SplitPlacement : std::uint8_t {
    Before,
    After
};

struct ViewState {
    ViewId id{};
    core::DocumentId document{};
    std::size_t caretOffset{0};
    std::size_t anchorOffset{0};
    std::size_t firstVisibleLine{0};
    std::optional<bool> wordWrapOverride;
    std::optional<bool> lineNumbersOverride;
    std::optional<std::string> fontFamilyOverride;
    std::optional<double> fontPointSizeOverride;
};

struct PaneState {
    PaneId id{};
    std::vector<ViewId> views;
    std::optional<ViewId> activeView;
};

class WorkspaceNode final {
public:
    enum class Kind : std::uint8_t {
        Pane,
        Split
    };

    [[nodiscard]] Kind kind() const noexcept;
    [[nodiscard]] PaneId pane() const noexcept;
    [[nodiscard]] SplitId split() const noexcept;
    [[nodiscard]] SplitOrientation orientation() const noexcept;
    [[nodiscard]] double ratio() const noexcept;
    [[nodiscard]] const WorkspaceNode* first() const noexcept;
    [[nodiscard]] const WorkspaceNode* second() const noexcept;

private:
    friend class WorkspaceModel;

    static std::unique_ptr<WorkspaceNode> makePane(PaneId pane);
    static std::unique_ptr<WorkspaceNode> makeSplit(SplitId split,
                                                    SplitOrientation orientation,
                                                    double ratio,
                                                    std::unique_ptr<WorkspaceNode> first,
                                                    std::unique_ptr<WorkspaceNode> second);

    Kind kind_{Kind::Pane};
    PaneId pane_{};
    SplitId split_{};
    SplitOrientation orientation_{SplitOrientation::Horizontal};
    double ratio_{0.5};
    std::unique_ptr<WorkspaceNode> first_;
    std::unique_ptr<WorkspaceNode> second_;
};

struct WorkspaceSnapshotNode final {
    WorkspaceNode::Kind kind{WorkspaceNode::Kind::Pane};
    PaneId pane{};
    SplitId split{};
    SplitOrientation orientation{SplitOrientation::Horizontal};
    double ratio{0.5};
    std::unique_ptr<WorkspaceSnapshotNode> first;
    std::unique_ptr<WorkspaceSnapshotNode> second;
};

struct WorkspaceSnapshot final {
    PaneId primaryPane{};
    PaneId activePane{};
    std::vector<PaneState> panes;
    std::vector<ViewState> views;
    std::unique_ptr<WorkspaceSnapshotNode> root;
    std::uint64_t nextPaneId{1};
    std::uint64_t nextViewId{1};
    std::uint64_t nextSplitId{1};
};

struct SplitResult {
    PaneId pane{};
    SplitId split{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(pane) && static_cast<bool>(split);
    }
};

class WorkspaceModel final {
public:
    static constexpr double minimumSplitRatio = 0.05;
    static constexpr double maximumSplitRatio = 0.95;

    WorkspaceModel();

    [[nodiscard]] PaneId primaryPane() const noexcept;
    [[nodiscard]] PaneId activePane() const noexcept;
    [[nodiscard]] const WorkspaceNode& root() const noexcept;

    [[nodiscard]] const PaneState* pane(PaneId id) const noexcept;
    [[nodiscard]] const ViewState* view(ViewId id) const noexcept;
    [[nodiscard]] ViewState* view(ViewId id) noexcept;
    [[nodiscard]] std::optional<PaneId> paneContaining(ViewId view) const noexcept;

    [[nodiscard]] ViewId openView(core::DocumentId document, PaneId pane);
    [[nodiscard]] bool closeView(ViewId view) noexcept;
    [[nodiscard]] bool moveView(ViewId view, PaneId targetPane,
                                std::optional<std::size_t> targetIndex = std::nullopt);
    [[nodiscard]] bool setActiveView(PaneId pane, ViewId view) noexcept;
    [[nodiscard]] bool setActivePane(PaneId pane) noexcept;

    [[nodiscard]] SplitResult splitPane(PaneId pane,
                                        SplitOrientation orientation,
                                        SplitPlacement placement = SplitPlacement::After,
                                        double ratio = 0.5);
    [[nodiscard]] bool setSplitRatio(SplitId split, double ratio) noexcept;
    [[nodiscard]] bool normalizeAlignedTwoByTwoRows(double alignmentTolerance = 0.02) noexcept;
    [[nodiscard]] bool normalizeAlignedTwoByTwoColumns(double alignmentTolerance = 0.02) noexcept;
    [[nodiscard]] bool removeEmptyPane(PaneId pane) noexcept;

    [[nodiscard]] std::size_t paneCount() const noexcept;
    [[nodiscard]] std::size_t viewCount() const noexcept;

    template <typename Visitor>
    void forEachPane(Visitor&& visitor) const {
        for (const auto& [id, pane] : panes_) {
            static_cast<void>(id);
            visitor(pane);
        }
    }

    template <typename Visitor>
    void forEachView(Visitor&& visitor) const {
        for (const auto& [id, view] : views_) {
            static_cast<void>(id);
            visitor(view);
        }
    }
    [[nodiscard]] WorkspaceSnapshot snapshot() const;
    [[nodiscard]] bool restore(const WorkspaceSnapshot& snapshot);

private:
    [[nodiscard]] PaneId allocatePaneId() noexcept;
    [[nodiscard]] ViewId allocateViewId() noexcept;
    [[nodiscard]] SplitId allocateSplitId() noexcept;

    [[nodiscard]] WorkspaceNode* findPaneNode(WorkspaceNode* node, PaneId pane) noexcept;
    [[nodiscard]] WorkspaceNode* findSplitNode(WorkspaceNode* node, SplitId split) noexcept;
    [[nodiscard]] bool removePaneNode(std::unique_ptr<WorkspaceNode>& node, PaneId pane) noexcept;
    [[nodiscard]] static bool validRatio(double ratio) noexcept;
    static void eraseViewFromPane(PaneState& pane, ViewId view) noexcept;
    [[nodiscard]] static std::unique_ptr<WorkspaceSnapshotNode> snapshotNode(
        const WorkspaceNode* node);
    [[nodiscard]] static std::unique_ptr<WorkspaceNode> restoreNode(
        const WorkspaceSnapshotNode* node);
    [[nodiscard]] static bool validateSnapshot(const WorkspaceSnapshot& snapshot);

    std::map<PaneId, PaneState> panes_;
    std::map<ViewId, ViewState> views_;
    std::unordered_map<std::uint64_t, PaneId> viewOwners_;
    std::unique_ptr<WorkspaceNode> root_;
    PaneId primaryPane_{};
    PaneId activePane_{};
    std::uint64_t nextPaneId_{1};
    std::uint64_t nextViewId_{1};
    std::uint64_t nextSplitId_{1};
};

}
