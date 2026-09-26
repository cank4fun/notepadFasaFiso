#pragma once

#include "WxTheme.hpp"
#include "notepadFasaFiso/gui/EditorHost.hpp"
#include "notepadFasaFiso/gui/GuiPolishModel.hpp"
#include "notepadFasaFiso/gui/TextAppearanceUndoJournal.hpp"
#include "notepadFasaFiso/gui/SelectionAppearanceModel.hpp"
#include "notepadFasaFiso/core/LinkDetector.hpp"
#include "notepadFasaFiso/settings/AppSettings.hpp"
#include "notepadFasaFiso/viewer/HexPreview.hpp"
#include "notepadFasaFiso/viewer/LargeFileViewer.hpp"
#include "notepadFasaFiso/viewer/LiveFileFollower.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include <string>

class wxPanel;
class wxScrollBar;
class wxScrollEvent;
class wxContextMenuEvent;
class wxMouseEvent;
class wxStyledTextCtrl;
class wxStyledTextEvent;
class wxWindow;

namespace nff::gui::wxbackend {

class WxEditorHost final : public IEditorHost {
public:
    using SynchronizeRequest = std::function<void()>;
    using ActivateRequest = std::function<void(workspace::ViewId)>;
    using ContextMenuRequest = std::function<void(wxWindow&, workspace::ViewId)>;
    using LinkActivateRequest = std::function<void(workspace::ViewId, const core::LinkSpan&)>;
    using FileDropRequest = std::function<bool(
        workspace::ViewId, const std::vector<std::filesystem::path>&)>;

    WxEditorHost(wxWindow& parent,
                 workspace::ViewId view,
                 IEditorHostEvents& events,
                 const WxThemePalette& theme,
                 const settings::AppSettings& settings,
                 SynchronizeRequest synchronizeRequest,
                 ActivateRequest activateRequest,
                 ContextMenuRequest contextMenuRequest,
                 LinkActivateRequest linkActivateRequest,
                 FileDropRequest fileDropRequest);
    ~WxEditorHost() override;

    WxEditorHost(const WxEditorHost&) = delete;
    WxEditorHost& operator=(const WxEditorHost&) = delete;

    void bind(const EditorHostBinding& binding) override;
    void restoreViewState(const EditorHostViewState& state) override;
    void setBounds(Rect bounds) override;
    void setVisible(bool visible) override;
    void setFocused(bool focused) override;
    [[nodiscard]] EditorHostRuntime runtime() const noexcept override;
    [[nodiscard]] bool execute(EditorHostCommand command) override;
    void refreshAppearance() override;
    [[nodiscard]] bool recordTextAppearanceUndo(
        std::uint64_t begin,
        std::uint64_t end,
        std::span<const metadata::TextAppearanceSpan> before,
        std::span<const metadata::TextAppearanceSpan> after) override;
    [[nodiscard]] EditorHostPollResult pollLiveContent() override;
    [[nodiscard]] EditorHostSearchResult findText(
        const EditorHostSearchRequest& request) override;
    [[nodiscard]] bool revealTextMatch(search::SearchMatch match,
                                       std::string_view pattern) override;
    [[nodiscard]] bool replaceTextRange(
        search::SearchMatch match,
        std::string_view replacement,
        metadata::TextColorEditPolicy colorPolicy) override;
    [[nodiscard]] bool replaceAllText(
        std::string_view text,
        metadata::TextColorEditPolicy colorPolicy) override;
    [[nodiscard]] std::error_code goToLine(std::uint64_t oneBasedLine) override;
    [[nodiscard]] std::error_code goToPosition(std::uint64_t oneBasedLine,
                                               std::uint64_t oneBasedColumn) override;

private:
    static constexpr int virtualScrollRange = 1'000'000;
    static constexpr int virtualScrollThumb = 20'000;
    static constexpr int virtualScrollPage = 100'000;
    static constexpr std::uint64_t hexRowsPerWindow = 512U;

    void onModified(wxStyledTextEvent& event);
    void onUpdateUi(wxStyledTextEvent& event);
    void onLeftDown(wxMouseEvent& event);
    void onMouseMove(wxMouseEvent& event);
    void onMouseLeave(wxMouseEvent& event);
    void onViewportScroll(wxScrollEvent& event);
    void onMouseWheel(wxMouseEvent& event);
    void onContextMenu(wxContextMenuEvent& event);
    void onIndicatorClick(wxStyledTextEvent& event);
    void requestSynchronize();

    void bindEditor(const EditorHostBinding& binding);
    void bindViewer(const EditorHostBinding& binding);
    void bindHex(const EditorHostBinding& binding);
    void reloadText(const EditorHostBinding& binding);
    void replaceReadOnlyText(const std::string& text);
    void loadViewerOffset(std::uint64_t byteOffset, std::size_t maximumBytes = 0U);
    [[nodiscard]] bool loadFollowTail(const viewer::TailWindowResult& tail);
    void loadHexRow(std::uint64_t firstRow);
    [[nodiscard]] std::string renderHexWindow(const viewer::HexWindow& window) const;
    [[nodiscard]] std::size_t viewerWindowBytes() const noexcept;
    [[nodiscard]] std::uint64_t scrollPositionToOffset(int position,
                                                       std::uint64_t maximum) const noexcept;
    [[nodiscard]] int offsetToScrollPosition(std::uint64_t offset,
                                             std::uint64_t maximum) const noexcept;
    void updateExternalScrollbar(std::uint64_t offset, std::uint64_t maximum);
    [[nodiscard]] int viewerDisplayLineCount() const noexcept;
    void positionViewerAfterWindowShift(bool movingBackward);
    void configureModeChrome();
    void layoutChildren();
    void applyAppearance();
    void applyTextAppearance(const EditorHostBinding& binding);
    [[nodiscard]] metadata::TextAppearanceMap currentAppearanceMap() const;
    [[nodiscard]] metadata::TextAppearanceMap adjustedTextAppearance(
        const EditorTextEdit& edit) const;
    void noteLocalAppearanceEdit(const EditorTextEdit& edit, bool mayCoalesce);
    [[nodiscard]] bool addTextAppearanceUndoAction(
        std::uint64_t begin,
        std::uint64_t end,
        std::span<const metadata::TextAppearanceSpan> before,
        std::span<const metadata::TextAppearanceSpan> after,
        bool mayCoalesce);
    [[nodiscard]] bool restoreTextAppearanceUndo(
        int token, TextAppearanceUndoDirection direction);
    void clearAppearanceStyles();
    void restyleTextAppearance();
    void refreshSpoilerIndicators();
    void updateSpoilerRevealAt(int position);
    void refreshLinkIndicators(bool force = false);
    void clearLinkIndicators();
    void updateLineNumberMargin(bool visible);
    void applyPendingRestore();

    wxPanel* container_{};
    wxStyledTextCtrl* control_{};
    wxScrollBar* externalScroll_{};
    IEditorHostEvents* events_{};
    const WxThemePalette* theme_{};
    const settings::AppSettings* settings_{};
    SynchronizeRequest synchronizeRequest_{};
    ActivateRequest activateRequest_{};
    ContextMenuRequest contextMenuRequest_{};
    LinkActivateRequest linkActivateRequest_{};
    FileDropRequest fileDropRequest_{};
    workspace::ViewId view_{};
    core::DocumentId document_{};
    std::filesystem::path path_{};
    core::OpenMode mode_{core::OpenMode::Editor};
    viewer::PerformanceProfile viewerPerformance_{viewer::PerformanceProfile::Automatic};
    viewer::LargeFileViewer viewer_{};
    std::unique_ptr<viewer::LiveFileFollower> follower_{};
    viewer::HexPreview hex_{};
    std::uint64_t viewerByteStart_{0U};
    std::uint64_t viewerByteEnd_{0U};
    std::uint64_t viewerFirstLine_{1U};
    std::uint64_t hexFirstRow_{0U};
    std::uint64_t documentRevision_{0U};
    EditorHostViewState pendingRestore_{};
    bool hasPendingRestore_{false};
    std::string fontFamily_;
    double fontPointSize_{12.0};
    int lineNumberMarginWidth_{-1};
    bool lineNumbersVisible_{false};
    bool suppressModified_{false};
    bool suppressExternalScroll_{false};
    bool forceReload_{true};
    bool followEnabled_{false};
    bool followWaiting_{false};
    bool visible_{false};
    bool focused_{false};
    core::DocumentId appearanceDocument_{};
    std::uint64_t appearanceRevision_{0U};
    std::vector<metadata::TextAppearanceSpan> appearanceSpans_{};
    std::vector<std::string> appearanceFontFamilies_{};
    bool hasSpoilers_{false};
    std::optional<AppearanceRange> revealedSpoiler_{};
    TextAppearanceUndoJournal appearanceUndoJournal_{};
    TextColorStyleRefreshLatch appearanceStyleRefresh_{};
    std::vector<core::LinkSpan> visibleLinks_{};
    int linkIndicatorStart_{0};
    int linkIndicatorLength_{0};
    int linkFirstVisibleLine_{-1};
    int linkLastVisibleLine_{-1};
    std::uint64_t linkRevision_{0U};
    bool forceLinkRefresh_{true};
    NativeChangeLatch<std::array<int, 4>> containerBoundsLatch_{};
    NativeChangeLatch<std::array<int, 4>> controlBoundsLatch_{};
    NativeChangeLatch<std::array<int, 4>> scrollBoundsLatch_{};
    NativeChangeLatch<bool> wordWrapLatch_{};
};

class WxEditorHostFactory final : public IEditorHostFactory {
public:
    using SynchronizeRequest = WxEditorHost::SynchronizeRequest;
    using ActivateRequest = WxEditorHost::ActivateRequest;
    using ContextMenuRequest = WxEditorHost::ContextMenuRequest;
    using LinkActivateRequest = WxEditorHost::LinkActivateRequest;
    using FileDropRequest = WxEditorHost::FileDropRequest;

    WxEditorHostFactory(wxWindow& parent,
                        const WxThemePalette& theme,
                        const settings::AppSettings& settings,
                        SynchronizeRequest synchronizeRequest,
                        ActivateRequest activateRequest,
                        ContextMenuRequest contextMenuRequest,
                        LinkActivateRequest linkActivateRequest,
                        FileDropRequest fileDropRequest);

    [[nodiscard]] std::unique_ptr<IEditorHost> create(
        workspace::ViewId view,
        IEditorHostEvents& events) override;

private:
    wxWindow* parent_{};
    const WxThemePalette* theme_{};
    const settings::AppSettings* settings_{};
    SynchronizeRequest synchronizeRequest_{};
    ActivateRequest activateRequest_{};
    ContextMenuRequest contextMenuRequest_{};
    LinkActivateRequest linkActivateRequest_{};
    FileDropRequest fileDropRequest_{};
};

}
