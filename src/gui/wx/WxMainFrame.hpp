#pragma once

#include "WxTheme.hpp"
#include "notepadFasaFiso/app/ApplicationLifecycle.hpp"
#include "notepadFasaFiso/app/CommandSystem.hpp"
#include "notepadFasaFiso/app/LaunchRequest.hpp"
#include "notepadFasaFiso/app/LaunchRouter.hpp"
#include "notepadFasaFiso/app/PresentationModel.hpp"
#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/core/LinkDetector.hpp"
#include "notepadFasaFiso/fonts/FontManager.hpp"
#include "notepadFasaFiso/gui/GuiRuntime.hpp"
#include "notepadFasaFiso/gui/GuiShell.hpp"
#include "notepadFasaFiso/metadata/MetadataStore.hpp"
#include "notepadFasaFiso/search/FileSearch.hpp"
#include "notepadFasaFiso/search/TextSearch.hpp"
#include "notepadFasaFiso/settings/AppSettings.hpp"
#include "notepadFasaFiso/workspace/WorkspaceModel.hpp"

#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
#include "notepadFasaFiso/gui/automation/AutomationProtocol.hpp"
#endif

#include <wx/frame.h>
#include <wx/timer.h>
#include <wx/treectrl.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <map>
#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

class wxActivateEvent;
class wxButton;
class wxCloseEvent;
class wxCommandEvent;
class wxKeyEvent;
class wxMenu;
class wxMouseCaptureLostEvent;
class wxMouseEvent;
class wxPaintEvent;
class wxPanel;
class wxStaticText;
class wxTextCtrl;

namespace nff::gui::automation::windows {
class AutomationPipeServer;
}

namespace nff::gui::wxbackend {

class WxEditorHostFactory;
class WxSearchDialog;
class WxTitleBar;
class WxTrayIcon;

class WxMainFrame final : public wxFrame {
public:
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
    explicit WxMainFrame(app::LaunchRequest startupRequest = {},
                         std::optional<std::filesystem::path> automationStateRoot = std::nullopt);
#else
    explicit WxMainFrame(app::LaunchRequest startupRequest = {});
#endif
    ~WxMainFrame() override;

#ifdef __WXMSW__
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

private:
    void onSize(wxSizeEvent& event);
    void onFramePaint(wxPaintEvent& event);
    void onClose(wxCloseEvent& event);
    void onActivate(wxActivateEvent& event);
    void onMaintenanceTimer(wxTimerEvent& event);
    void onSidebarQuery(wxCommandEvent& event);
    void onSidebarSearchTimer(wxTimerEvent& event);
    void onDragPreviewTimer(wxTimerEvent& event);
    void onSidebarTreeActivated(wxTreeEvent& event);
    void onSidebarTreeExpanding(wxTreeEvent& event);
    void onSidebarTreeSelectionChanged(wxTreeEvent& event);
    void onSidebarTreeLeftDown(wxMouseEvent& event);
    void onSidebarTreeMotion(wxMouseEvent& event);
    void onSidebarTreeLeftUp(wxMouseEvent& event);
    void onSidebarTreeCaptureLost(wxMouseCaptureLostEvent& event);
    void onSidebarSearchKeyDown(wxKeyEvent& event);
    void onSidebarSplitterDown(wxMouseEvent& event);
    void onSidebarSplitterMotion(wxMouseEvent& event);
    void onSidebarSplitterUp(wxMouseEvent& event);
    void onWorkspaceSplitterDown(wxMouseEvent& event, workspace::SplitId split);
    void updateWorkspaceSplitterDragFromPointer();
    void updateSidebarSplitterDragFromPointer();
    void updateDragPreviewTimerState() noexcept;
    void onTabLeftDown(wxMouseEvent& event, workspace::ViewId view,
                       workspace::PaneId pane);
    void onTabMotion(wxMouseEvent& event);
    void onTabLeftUp(wxMouseEvent& event);
    void onTabCaptureLost(wxMouseCaptureLostEvent& event);

    void scheduleSynchronize();
    void synchronizeNow();
    void updateViewport() noexcept;
    void createInitialDocument();
    void initializeWorkspace();
    void applyStartupLaunch();
    void applyStartupPositions();
    [[nodiscard]] std::optional<workspace::ViewId> openStartupTarget(
        const app::RoutedTarget& target);
    void showStartupRecoveryPrompt();
    [[nodiscard]] std::error_code recoverUnclaimedSnapshots();
    [[nodiscard]] std::error_code discardActiveRecovery();
    void createChrome();
    void bindCommands();
    void installAccelerators();
    void setupBranding();
    void updateChrome(const GuiFrame& frame);
    void updateStatus(const app::ApplicationPresentationSnapshot& presentation);
    void updateExternalNotice(const app::ApplicationPresentationSnapshot& presentation);
    void updateSidebar(const GuiFrame& frame);
    void updatePaneTabs(const GuiFrame& frame);
    void updateThemeControls();
    void applyLayoutDensity();
    void updateOuterFrameInset();
    void updateResizeZones() noexcept;
    void refreshEditorAppearance();
    void applyTheme(settings::ThemePreference preference);
    void applyAccent(settings::AccentPreference preference);
    void applyDensity(settings::UiDensity density);
    [[nodiscard]] std::error_code showSettingsDialog();
    void chooseEditorFont();
    void setEditorFontSize(double pointSize);
    void showTabContextMenu(wxWindow& anchor, workspace::ViewId view);
    void showEditorContextMenu(wxWindow& anchor, workspace::ViewId view);
    void activateDetectedLink(workspace::ViewId view, const core::LinkSpan& link);
    void chooseEditorFontForView(workspace::ViewId view);
    void setEditorFontSizeForView(workspace::ViewId view, double pointSize);
    void resetEditorAppearanceForView(workspace::ViewId view);
    void cancelTabDrag() noexcept;
    void cancelSidebarTreeDrag() noexcept;
    void updateSidebarTreeDropIndicator(int screenX, int screenY);
    void finishSidebarTreeDrag(const std::filesystem::path& path, int screenX, int screenY);
    void updateTabDropIndicator(int screenX, int screenY);
    void hideTabDropIndicator() noexcept;
    void showPaneDropIndicator(workspace::PaneId pane);
    void hidePaneDropIndicator() noexcept;
    void updateDraggedTabVisual() noexcept;
    void updateWorkspaceSplitters(const GuiFrame& frame);
    void updateWorkspaceSplitterPreview(const SplitterLayout& splitter, double ratio);
    void hideWorkspaceSplitterPreview() noexcept;
    void updateSidebarSplitterPreview(double width);
    void hideSidebarSplitterPreview() noexcept;
    void cancelSidebarSplitterDrag() noexcept;
    void cancelWorkspaceSplitterDrag() noexcept;
    void finishTabDrag(workspace::ViewId view, workspace::PaneId sourcePane,
                       int screenX, int screenY);
    [[nodiscard]] std::optional<std::size_t> tabDropIndex(
        workspace::PaneId pane, workspace::ViewId movingView, int screenX) const;

    void showFileMenu(wxWindow& anchor);
    void showEditMenu(wxWindow& anchor);
    void showViewMenu(wxWindow& anchor);
    void showFormatMenu(wxWindow& anchor);
    void appendCommand(wxMenu& menu,
                       int uiId,
                       const wxString& label,
                       app::CommandId command,
                       bool checkable = false);
    void appendRadioCommand(wxMenu& menu,
                            int uiId,
                            const wxString& label,
                            app::CommandId command);
    void dispatch(app::CommandId command);

    [[nodiscard]] std::error_code newDocument();
    [[nodiscard]] std::error_code newDocumentInPane(workspace::PaneId pane);
    [[nodiscard]] std::error_code openFilesDialog();
    [[nodiscard]] std::error_code chooseSidebarFolder();
    [[nodiscard]] std::error_code reindexSidebarFolder();
    [[nodiscard]] std::error_code clearSidebarFolder();
    [[nodiscard]] std::error_code saveActive();
    [[nodiscard]] std::error_code saveActiveOverwriteExternal();
    [[nodiscard]] std::error_code reloadActive();
    [[nodiscard]] std::error_code reopenActiveAsEncoding(encoding::Encoding encoding);
    [[nodiscard]] std::error_code saveActiveAs(bool copy);
    [[nodiscard]] std::error_code closeActiveView();
    [[nodiscard]] std::error_code closeView(workspace::ViewId view);
    [[nodiscard]] std::error_code closeActivePane();
    [[nodiscard]] std::error_code splitActive(workspace::SplitOrientation orientation);
    [[nodiscard]] std::error_code toggleSidebar();
    [[nodiscard]] std::error_code toggleTabs();
    [[nodiscard]] std::error_code toggleWordWrap();
    [[nodiscard]] std::error_code toggleLineNumbers();
    [[nodiscard]] std::error_code toggleLinkDetection();
    [[nodiscard]] std::error_code switchActiveToEditor();
    [[nodiscard]] std::error_code switchActiveToViewer();
    [[nodiscard]] std::error_code switchActiveToHexPreview();
    [[nodiscard]] std::error_code toggleFollow();
    [[nodiscard]] std::error_code setViewerPerformance(viewer::PerformanceProfile profile);
    [[nodiscard]] std::error_code setDocumentEncoding(encoding::Encoding encoding);
    [[nodiscard]] std::error_code toggleDocumentBom();
    [[nodiscard]] std::error_code convertLineEndings(core::LineEndingPolicy policy);
    [[nodiscard]] std::error_code validateActiveJson();
    [[nodiscard]] std::error_code formatActiveJson(bool pretty);
    [[nodiscard]] std::error_code showConvertExportDialog();
    [[nodiscard]] std::error_code applyTextUtility(app::CommandId command);
    [[nodiscard]] std::error_code chooseSelectionTextColor();
    [[nodiscard]] std::error_code clearSelectionTextColor();
    [[nodiscard]] std::error_code chooseSelectionFontFamily();
    [[nodiscard]] std::error_code clearSelectionFontFamily();
    [[nodiscard]] std::error_code chooseSelectionFontSize();
    [[nodiscard]] std::error_code clearSelectionFontSize();
    [[nodiscard]] std::error_code setSelectionSpoiler(bool enabled);
    [[nodiscard]] std::error_code resetSelectionAppearance();
    void ensureTextAppearanceState(core::DocumentId document);
    [[nodiscard]] EditorHostAppearance appearanceForDocument(core::DocumentId document);
    void noteTextAppearanceEdit(core::DocumentId document, const EditorTextEdit& edit);
    [[nodiscard]] bool restoreTextAppearanceRange(
        core::DocumentId document,
        std::uint64_t begin,
        std::uint64_t end,
        std::span<const metadata::TextAppearanceSpan> spans);
    void checkpointTextAppearance(core::DocumentId document);
    [[nodiscard]] std::error_code persistTextAppearance(core::DocumentId document);
    void resetTextAppearance(core::DocumentId document);
    [[nodiscard]] std::error_code commitSelectionAppearance(
        core::DocumentId document,
        workspace::ViewId view,
        std::uint64_t begin,
        std::uint64_t end,
        metadata::TextAppearanceMap candidate,
        std::span<const metadata::TextAppearanceSpan> before);
    [[nodiscard]] std::error_code editorCommand(EditorHostCommand command);
    [[nodiscard]] std::error_code showSearchDialog(bool replaceMode);
    [[nodiscard]] std::error_code findFromSession(search::SearchDirection direction);
    [[nodiscard]] std::error_code replaceCurrentMatch();
    [[nodiscard]] std::error_code replaceAllMatches();
    [[nodiscard]] std::error_code showGoToLineDialog();
    void setSearchStatus(const std::string& status);

    [[nodiscard]] bool openPath(const std::filesystem::path& path);
    [[nodiscard]] bool openPathInPane(const std::filesystem::path& path, workspace::PaneId pane);
    [[nodiscard]] bool openDroppedFiles(
        const std::vector<std::filesystem::path>& paths,
        std::optional<workspace::PaneId> targetPane = std::nullopt);
    [[nodiscard]] std::error_code startSidebarIndex(const std::filesystem::path& root);
    void stopSidebarIndex() noexcept;
    void processSidebarIndexCompletion();
    void updateSidebarHint();
    void resetSidebarTree();
    void refreshSidebarTreePreservingState();
    void collectExpandedSidebarPaths(const wxTreeItemId& item,
                                     std::vector<std::filesystem::path>& paths) const;
    [[nodiscard]] wxTreeItemId findSidebarTreeItem(const std::filesystem::path& path);
    [[nodiscard]] wxTreeItemId findSidebarChild(const wxTreeItemId& parent,
                                                const std::filesystem::path& path) const;
    [[nodiscard]] wxTreeItemId expandSidebarPath(const std::filesystem::path& path);
    void populateSidebarDirectory(const wxTreeItemId& item,
                                  const std::filesystem::path& directory);
    void refreshSidebarSearchNow(std::string_view query);
    void requestSidebarSearch(std::string query);
    void ensureSidebarSearchWorker();
    void stopSidebarSearch() noexcept;
    void processSidebarSearchCompletion();
    void rebuildSidebarSearchTree(std::string_view query,
                                  const std::vector<search::FileSearchHit>& hits);
    [[nodiscard]] std::filesystem::path sidebarSearchScope() const;
    [[nodiscard]] bool openSidebarSelection();
    [[nodiscard]] std::optional<workspace::ViewId> activeView() const noexcept;
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
    [[nodiscard]] std::vector<automation::AutomationElementSnapshot> collectAutomationElements();
    [[nodiscard]] automation::AutomationStatusSnapshot collectAutomationStatus() const;
    [[nodiscard]] automation::AutomationWindowSnapshot collectAutomationWindow();
    [[nodiscard]] std::string handleAutomationBridgeRequest(std::string_view request);
    [[nodiscard]] std::string buildAutomationBridgeResponse(const automation::BridgeCommand& command);
#endif
    [[nodiscard]] std::optional<core::DocumentId> activeDocumentId() const noexcept;
    [[nodiscard]] core::Document* activeDocument() noexcept;
    [[nodiscard]] const core::Document* activeDocument() const noexcept;
    [[nodiscard]] bool documentStillViewed(core::DocumentId document) const;
    [[nodiscard]] bool documentViewedOutsideView(core::DocumentId document,
                                                 workspace::ViewId excluded) const;
    [[nodiscard]] bool documentViewedOutsidePane(core::DocumentId document,
                                                 workspace::PaneId excluded) const;
    [[nodiscard]] bool documentHasFollowingView(core::DocumentId document) const;
    void showError(const wxString& title, const std::error_code& error);
    void updateWindowTitle(const app::ApplicationPresentationSnapshot& presentation);
    void persistSettings() noexcept;
    [[nodiscard]] app::PersistentSaveResult persistApplicationState() noexcept;
    void handleMaintenance(const app::ApplicationMaintenanceResult& result);

    app::ApplicationPaths applicationPaths_;
    app::LaunchRequest startupRequest_{};
    app::LaunchPlan startupLaunch_{};
    core::DocumentManager documents_{};
    workspace::WorkspaceModel workspace_{};
    app::ApplicationLifecycle lifecycle_;
    settings::AppSettings& settings_;
    metadata::MetadataStore metadataStore_;
    struct DocumentAppearanceState final {
        metadata::TextAppearanceMap appearance;
        std::uint64_t revision{1U};
        bool loaded{false};
    };
    std::map<core::DocumentId, DocumentAppearanceState> appearanceStates_{};
    fonts::FontManager fontManager_{};
    search::FileSearchIndex fileSearch_{};
    app::PresentationModel presentation_;
    GuiShell shell_;
    WxThemePalette theme_;
    app::CommandDispatcher dispatcher_{};
    std::unique_ptr<WxEditorHostFactory> hostFactory_{};
    std::unique_ptr<GuiRuntime> runtime_{};
    std::unique_ptr<WxTrayIcon> trayIcon_{};
    WxSearchDialog* searchDialog_{};
    struct SearchSession final {
        std::string pattern;
        std::string replacement;
        search::SearchOptions options{};
        std::optional<search::SearchMatch> match;
        core::DocumentId document{};
        std::uint64_t revision{0U};
    } searchSession_{};
    wxTimer maintenanceTimer_{};
    wxTimer sidebarSearchTimer_{};
    wxTimer dragPreviewTimer_{};
    std::error_code lastMaintenanceError_{};

    WxTitleBar* titleBar_{};
    wxPanel* resizeTop_{};
    wxPanel* resizeBottom_{};
    wxPanel* resizeLeft_{};
    wxPanel* resizeRight_{};
    wxPanel* topBar_{};
    wxButton* fileButton_{};
    wxButton* editButton_{};
    wxButton* viewButton_{};
    wxButton* formatButton_{};
    wxStaticText* brandText_{};
    wxTextCtrl* sidebarSearch_{};
    wxTreeCtrl* sidebarTree_{};
    wxPanel* sidebarSplitter_{};
    wxPanel* externalNoticePanel_{};
    wxStaticText* externalNoticeText_{};
    wxButton* externalReloadButton_{};
    wxButton* externalSaveMineButton_{};
    wxButton* externalSaveAsButton_{};
    wxPanel* statusPanel_{};
    wxStaticText* statusPositionText_{};
    wxStaticText* statusEncodingText_{};
    wxStaticText* statusLineEndingText_{};
    wxStaticText* statusFormatText_{};
    wxStaticText* statusModeText_{};
    wxStaticText* statusExtraText_{};
    std::map<workspace::PaneId, wxPanel*> tabPanels_{};
    struct TabWidget final {
        wxWindow* button{};
        workspace::PaneId pane{};
        workspace::ViewId view{};
    };
    std::vector<TabWidget> tabWidgets_{};
    std::map<workspace::PaneId, wxWindow*> tabAddButtons_{};
    std::array<wxWindow*, 4> tabDropPanePreview_{};
    struct SplitterWidget final {
        wxPanel* panel{};
        workspace::SplitId split{};
        workspace::SplitOrientation orientation{workspace::SplitOrientation::Horizontal};
    };
    std::vector<SplitterWidget> workspaceSplitterWidgets_{};
    std::string workspaceSplitterFingerprint_{};
    wxWindow* workspaceSplitPreview_{};
    std::vector<std::filesystem::path> unclaimedRecoverySnapshots_{};
    struct PendingStartupPosition final {
        workspace::ViewId view{};
        app::TextPosition position{};
    };
    std::vector<PendingStartupPosition> pendingStartupPositions_{};
    std::vector<std::string> startupLaunchIssues_{};
    std::filesystem::path sidebarRoot_{};
    std::filesystem::path sidebarScope_{};
    std::jthread sidebarIndexThread_{};
    std::jthread sidebarSearchThread_{};
    std::mutex sidebarSearchMutex_{};
    std::condition_variable_any sidebarSearchCv_{};
    std::string sidebarPendingSearchQuery_{};
    std::string sidebarCompletedSearchQuery_{};
    std::vector<search::FileSearchHit> sidebarCompletedSearchHits_{};
    std::atomic<std::uint64_t> sidebarSearchRequestSerial_{0U};
    std::uint64_t sidebarSearchCompletedSerial_{0U};
    std::uint64_t sidebarSearchAppliedSerial_{0U};
    search::FileSearchBuildResult sidebarIndexResult_{};
    std::atomic_bool sidebarIndexReady_{false};
    std::size_t sidebarIndexedFiles_{0U};
    bool sidebarIndexTruncated_{false};
    bool sidebarIndexing_{false};
    bool sidebarIndexAvailable_{false};
    bool sidebarRootUnavailable_{false};
    std::string sidebarSearchQuery_{};
    std::string sidebarQueryCache_{};
    std::string sidebarScopeCache_{};
    bool sidebarSearchTreeMode_{false};
    bool sidebarTreeRefreshing_{false};
    std::uint64_t sidebarGenerationCache_{0U};
    std::optional<std::uint64_t> tabsFingerprint_{};
    std::string windowTitleCache_{};
    std::optional<std::filesystem::path> sidebarTreeDragPath_{};
    int sidebarTreeDragStartScreenX_{0};
    int sidebarTreeDragStartScreenY_{0};
    bool sidebarTreeDragActive_{false};
    std::optional<workspace::ViewId> tabDragView_{};
    workspace::PaneId tabDragSourcePane_{};
    int tabDragStartScreenX_{0};
    int tabDragStartScreenY_{0};
    bool tabDragActive_{false};
    std::optional<workspace::SplitId> workspaceSplitDrag_{};
    std::optional<double> workspaceSplitDragRatio_{};
    std::optional<double> sidebarSplitDragWidth_{};
    wxWindow* sidebarSplitPreview_{};
    bool sidebarDragging_{false};
    bool synchronizePending_{false};
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
    std::shared_ptr<std::atomic<WxMainFrame*>> automationFrameRef_{};
    std::unique_ptr<automation::windows::AutomationPipeServer> automationPipe_{};
#endif
    bool shuttingDown_{false};
};

}
