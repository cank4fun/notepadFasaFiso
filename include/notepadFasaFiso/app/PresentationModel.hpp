#pragma once

#include "notepadFasaFiso/app/CommandSystem.hpp"
#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/settings/AppSettings.hpp"
#include "notepadFasaFiso/storage/FileState.hpp"
#include "notepadFasaFiso/workspace/WorkspaceModel.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nff::app {

struct ViewRuntimeState final {
    core::OpenMode openMode{core::OpenMode::Editor};
    viewer::PerformanceProfile viewerPerformance{viewer::PerformanceProfile::Automatic};
    viewer::PerformanceProfile resolvedViewerPerformance{viewer::PerformanceProfile::Automatic};
    bool initialized{false};
    bool followEnabled{false};
    bool followWaiting{false};
    bool hasSelection{false};
    bool canUndo{false};
    bool canRedo{false};
    bool canPaste{false};
    bool wordWrap{true};
    bool lineNumbersVisible{false};
    std::string fontFamily;
    double fontPointSize{12.0};
    std::uint64_t caretLine{1U};
    std::uint64_t caretColumn{1U};
    std::uint64_t selectionBytes{0U};
    std::uint64_t contentBytes{0U};
    std::uint64_t windowByteStart{0U};
    std::uint64_t windowByteEnd{0U};
    std::size_t viewerCacheResidentBytes{0U};
};

struct TabPresentation final {
    workspace::ViewId view{};
    core::DocumentId document{};
    std::string title;
    std::string path;
    bool active{false};
    bool modified{false};
    bool externalConflict{false};
    bool recovered{false};
    bool untitled{false};
};

struct PanePresentation final {
    workspace::PaneId pane{};
    std::vector<TabPresentation> tabs;
    std::optional<workspace::ViewId> activeView;
    bool active{false};
};

struct SplitPresentationNode final {
    workspace::WorkspaceNode::Kind kind{workspace::WorkspaceNode::Kind::Pane};
    workspace::PaneId pane{};
    workspace::SplitId split{};
    workspace::SplitOrientation orientation{workspace::SplitOrientation::Horizontal};
    double ratio{0.5};
    std::unique_ptr<SplitPresentationNode> first;
    std::unique_ptr<SplitPresentationNode> second;
};

struct StatusBarPresentation final {
    bool visible{false};
    std::uint64_t line{1U};
    std::uint64_t column{1U};
    std::uint64_t selectionBytes{0U};
    std::string encoding;
    bool writesBom{false};
    std::string lineEnding;
    std::string format;
    std::string mode;
    std::string viewerPerformance;
    std::string viewerResolvedPerformance;
    std::uint64_t contentBytes{0U};
    std::uint64_t windowByteStart{0U};
    std::uint64_t windowByteEnd{0U};
    std::size_t viewerCacheResidentBytes{0U};
    bool followEnabled{false};
    bool followWaiting{false};
    bool modified{false};
    bool externalConflict{false};
    bool recovered{false};
    storage::FileChangeState externalChangeState{storage::FileChangeState::Untracked};
    bool explicitOverwriteRequired{false};
};

struct SidebarPresentation final {
    bool visible{true};
    double width{280.0};
    bool folderActive{false};
};

struct CommandPresentation final {
    CommandId id{CommandId::NewDocument};
    std::string_view name;
    CommandState state{};
};

struct ApplicationPresentationSnapshot final {
    bool tabsVisible{true};
    workspace::PaneId activePane{};
    std::optional<workspace::ViewId> activeView;
    std::optional<core::DocumentId> activeDocument;
    std::vector<PanePresentation> panes;
    std::unique_ptr<SplitPresentationNode> splitRoot;
    SidebarPresentation sidebar;
    StatusBarPresentation status;
    std::vector<CommandPresentation> commands;
};

class PresentationModel final {
public:
    static constexpr double minimumSidebarWidth = 160.0;
    static constexpr double maximumSidebarWidth = 1200.0;

    PresentationModel(const core::DocumentManager& documents,
                      const workspace::WorkspaceModel& workspace,
                      const settings::AppSettings& settings) noexcept;

    void sync();

    [[nodiscard]] bool setViewRuntime(workspace::ViewId view,
                                      const ViewRuntimeState& state);
    [[nodiscard]] const ViewRuntimeState* viewRuntime(workspace::ViewId view) const noexcept;
    [[nodiscard]] bool eraseViewRuntime(workspace::ViewId view) noexcept;
    void setExternalConflict(core::DocumentId document, bool conflict) noexcept;
    void setExternalChangeState(core::DocumentId document,
                                storage::FileChangeState state) noexcept;
    [[nodiscard]] bool externalConflict(core::DocumentId document) const noexcept;
    [[nodiscard]] storage::FileChangeState externalChangeState(
        core::DocumentId document) const noexcept;
    void setRecovered(core::DocumentId document, bool recovered) noexcept;
    [[nodiscard]] bool recovered(core::DocumentId document) const noexcept;

    void setSidebarVisible(bool visible) noexcept;
    void setSidebarWidth(double width) noexcept;
    void setSidebarFolderActive(bool active) noexcept;

    [[nodiscard]] CommandContext commandContext() const;
    [[nodiscard]] ApplicationPresentationSnapshot snapshot() const;

private:
    [[nodiscard]] std::optional<workspace::ViewId> activeView() const noexcept;
    [[nodiscard]] const core::Document* documentFor(workspace::ViewId view) const noexcept;
    [[nodiscard]] ViewRuntimeState defaultRuntime(workspace::ViewId view) const noexcept;
    [[nodiscard]] const ViewRuntimeState& runtimeFor(workspace::ViewId view) const;
    [[nodiscard]] std::unique_ptr<SplitPresentationNode> buildSplitNode(
        const workspace::WorkspaceNode& node) const;
    [[nodiscard]] PanePresentation buildPane(const workspace::PaneState& pane) const;
    [[nodiscard]] TabPresentation buildTab(workspace::ViewId view,
                                           bool active) const;
    [[nodiscard]] StatusBarPresentation buildStatus() const;
    [[nodiscard]] std::vector<CommandPresentation> buildCommands() const;

    const core::DocumentManager* documents_{};
    const workspace::WorkspaceModel* workspace_{};
    const settings::AppSettings* settings_{};
    std::map<workspace::ViewId, ViewRuntimeState> viewRuntime_;
    std::map<core::DocumentId, storage::FileChangeState> externalChanges_;
    std::map<core::DocumentId, bool> recoveredDocuments_;
    SidebarPresentation sidebar_{};
};

}
