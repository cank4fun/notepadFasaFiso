#pragma once

#include "notepadFasaFiso/app/PresentationModel.hpp"
#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/gui/EditorHost.hpp"
#include "notepadFasaFiso/gui/GuiShell.hpp"
#include "notepadFasaFiso/workspace/WorkspaceModel.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string_view>
#include <system_error>

namespace nff::gui {

struct GuiRuntimePolicy final {
    std::size_t maximumDormantHosts{8U};
};

struct GuiRuntimeCallbacks final {
    std::function<void(core::DocumentId)> documentWillEdit;
    std::function<void(core::DocumentId, const EditorTextEdit&)> documentEdited;
    std::function<EditorHostAppearance(core::DocumentId)> appearance;
    std::function<bool(core::DocumentId,
                       std::uint64_t,
                       std::uint64_t,
                       std::span<const metadata::TextAppearanceSpan>)>
        appearanceRangeRestored;
};

struct GuiRuntimePollResult final {
    bool changed{false};
    std::size_t changedHosts{0U};
    std::error_code error{};

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

struct GuiRuntimeStats final {
    std::uint64_t generation{0U};
    std::size_t residentHosts{0U};
    std::size_t visibleHosts{0U};
    std::size_t dormantHosts{0U};
};

class GuiRuntime final : public IEditorHostEvents {
public:
    GuiRuntime(core::DocumentManager& documents,
               workspace::WorkspaceModel& workspace,
               app::PresentationModel& presentation,
               GuiShell& shell,
               IEditorHostFactory& hostFactory,
               GuiRuntimePolicy policy = {},
               GuiRuntimeCallbacks callbacks = {}) noexcept;

    void setPolicy(GuiRuntimePolicy policy) noexcept;
    [[nodiscard]] GuiRuntimePolicy policy() const noexcept;

    [[nodiscard]] const GuiFrame& synchronize();
    [[nodiscard]] IEditorHost* host(workspace::ViewId view) noexcept;
    [[nodiscard]] const IEditorHost* host(workspace::ViewId view) const noexcept;
    [[nodiscard]] GuiRuntimeStats stats() const noexcept;
    [[nodiscard]] bool executeEditorCommand(workspace::ViewId view,
                                            EditorHostCommand command);
    [[nodiscard]] EditorHostSearchResult findText(
        workspace::ViewId view, const EditorHostSearchRequest& request);
    [[nodiscard]] bool replaceTextRange(
        workspace::ViewId view,
        search::SearchMatch match,
        std::string_view replacement,
        metadata::TextColorEditPolicy colorPolicy = metadata::TextColorEditPolicy::AdjustRanges);
    [[nodiscard]] bool replaceAllText(
        workspace::ViewId view,
        std::string_view text,
        metadata::TextColorEditPolicy colorPolicy = metadata::TextColorEditPolicy::AdjustRanges);
    [[nodiscard]] std::error_code goToLine(workspace::ViewId view,
                                           std::uint64_t oneBasedLine);
    [[nodiscard]] std::error_code goToPosition(workspace::ViewId view,
                                               std::uint64_t oneBasedLine,
                                               std::uint64_t oneBasedColumn);
    void refreshHostAppearance();
    [[nodiscard]] GuiRuntimePollResult pollLiveContent();
    void invalidateDocument(core::DocumentId document) noexcept;

    [[nodiscard]] bool applyTextEdit(workspace::ViewId view,
                                     const EditorTextEdit& edit) override;
    [[nodiscard]] bool restoreTextAppearanceRange(
        workspace::ViewId view,
        std::uint64_t begin,
        std::uint64_t end,
        std::span<const metadata::TextAppearanceSpan> spans) override;

private:
    struct HostEntry final {
        std::unique_ptr<IEditorHost> host;
        std::uint64_t lastActiveGeneration{0U};
        bool visible{false};
    };

    [[nodiscard]] HostEntry* ensureHost(workspace::ViewId view);
    void synchronizeActiveHosts(const GuiFrame& frame);
    void hideDormantHosts();
    void pruneDormantHosts();
    [[nodiscard]] bool harvestRuntime(workspace::ViewId view, IEditorHost& host);
    [[nodiscard]] const PaneLayout* layoutForPane(const ShellLayout& layout,
                                                  workspace::PaneId pane) const noexcept;
    [[nodiscard]] EditorHostBinding bindingFor(workspace::ViewId view) const;
    [[nodiscard]] EditorHostViewState persistedViewState(workspace::ViewId view) const noexcept;

    core::DocumentManager* documents_{};
    workspace::WorkspaceModel* workspace_{};
    app::PresentationModel* presentation_{};
    GuiShell* shell_{};
    IEditorHostFactory* hostFactory_{};
    GuiRuntimePolicy policy_{};
    GuiRuntimeCallbacks callbacks_{};
    std::map<workspace::ViewId, HostEntry> hosts_;
    std::uint64_t generation_{0U};
};

}
