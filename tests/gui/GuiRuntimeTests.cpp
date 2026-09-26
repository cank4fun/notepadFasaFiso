#include "notepadFasaFiso/app/PresentationModel.hpp"
#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/gui/GuiRuntime.hpp"
#include "notepadFasaFiso/gui/TextColorUndoJournal.hpp"
#include "notepadFasaFiso/gui/TextAppearanceUndoJournal.hpp"
#include "notepadFasaFiso/settings/AppSettings.hpp"
#include "notepadFasaFiso/storage/FileWriter.hpp"
#include "notepadFasaFiso/workspace/WorkspaceModel.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view text) {
    const auto* begin = reinterpret_cast<const std::byte*>(text.data());
    return {begin, begin + text.size()};
}

template <typename Binding>
constexpr bool hasPreparedInspectionPointers = requires(Binding binding) {
    binding.documentProfile;
    binding.documentFileState;
};

template <typename Binding>
constexpr bool hasPreferredByteOffset = requires(Binding binding) {
    binding.preferredByteOffset;
};

template <typename Binding>
constexpr bool hasSelectionAppearanceBinding = requires(Binding binding) {
    binding.appearanceSpans;
    binding.appearanceFontFamilies;
    binding.appearanceRevision;
    binding.hasSpoilers;
};

void testTextColorUndoJournalRestoresSnapshots() {
    nff::gui::TextColorUndoJournal journal;
    const std::vector<nff::metadata::TextColorSpan> before{
        {0U, 3U, 0xFFFF0000U},
        {4U, 7U, 0xFF0000FFU},
    };
    const std::vector<nff::metadata::TextColorSpan> after{
        {1U, 4U, 0xFFFF0000U},
        {5U, 8U, 0xFF0000FFU},
    };

    const auto token = journal.record(before, after);
    const auto* undo = journal.snapshot(token, nff::gui::TextColorUndoDirection::Undo);
    const auto* redo = journal.snapshot(token, nff::gui::TextColorUndoDirection::Redo);
    expect(token > 0 && undo != nullptr && *undo == before,
           "text color undo journal restores pre-edit snapshot");
    expect(redo != nullptr && *redo == after,
           "text color undo journal restores post-edit snapshot");
    expect(journal.snapshot(token + 1000, nff::gui::TextColorUndoDirection::Undo) == nullptr,
           "unknown text color undo token is ignored safely");

    journal.clear();
    expect(journal.snapshot(token, nff::gui::TextColorUndoDirection::Undo) == nullptr,
           "clearing native undo state also clears metadata journal");
}

void testTextAppearanceUndoJournalStoresAffectedRangeDeltas() {
    nff::gui::TextAppearanceUndoJournal journal;
    nff::metadata::AppearanceStyle red;
    red.foregroundArgb = 0xFFFF0000U;
    nff::metadata::AppearanceStyle spoiler;
    spoiler.spoiler = true;
    const std::vector<nff::metadata::TextAppearanceSpan> before{{10U, 14U, red}};
    const std::vector<nff::metadata::TextAppearanceSpan> after{{10U, 12U, red}, {12U, 14U, spoiler}};

    const auto token = journal.record(10U, 14U, before, after);
    const auto* undo = journal.delta(token, nff::gui::TextAppearanceUndoDirection::Undo);
    const auto* redo = journal.delta(token, nff::gui::TextAppearanceUndoDirection::Redo);
    expect(token > 0 && undo != nullptr && undo->begin == 10U && undo->end == 14U && undo->spans == before,
           "appearance undo stores only affected pre-edit range fragment");
    expect(redo != nullptr && redo->spans == after,
           "appearance redo stores only affected post-edit range fragment");
}

void testEditorHostBindingCarriesPreparedInspectionApi() {
    expect(hasPreparedInspectionPointers<nff::gui::EditorHostBinding>,
           "editor host binding exposes prepared document inspection metadata");
    expect(hasPreferredByteOffset<nff::gui::EditorHostBinding>,
           "editor host binding exposes persisted preferred byte offset");
    expect(hasSelectionAppearanceBinding<nff::gui::EditorHostBinding>,
           "editor host binding carries shared document appearance and spoiler fast-path");
}

class FakeHost final : public nff::gui::IEditorHost {
public:
    explicit FakeHost(nff::workspace::ViewId view,
                      nff::gui::IEditorHostEvents& events) noexcept
        : view_(view), events_(&events) {}

    void bind(const nff::gui::EditorHostBinding& binding) override {
        binding_ = binding;
        boundText_.assign(binding.text);
        ++bindCount_;
    }

    void restoreViewState(const nff::gui::EditorHostViewState& state) override {
        runtime_.caretOffset = state.caretOffset;
        runtime_.anchorOffset = state.anchorOffset;
        runtime_.firstVisibleLine = state.firstVisibleLine;
        restored_ = true;
    }

    void setBounds(const nff::gui::Rect bounds) override { bounds_ = bounds; }
    void setVisible(const bool visible) override {
        visible_ = visible;
        ++visibleCallCount_;
        if (!visible) {
            ++hiddenCallCount_;
        }
    }
    void setFocused(const bool focused) override { focused_ = focused; }
    [[nodiscard]] nff::gui::EditorHostRuntime runtime() const noexcept override {
        return runtime_;
    }

    [[nodiscard]] bool execute(const nff::gui::EditorHostCommand command) override {
        lastCommand_ = command;
        ++commandCount_;
        return commandResult_;
    }

    void refreshAppearance() override { ++appearanceRefreshCount_; }

    [[nodiscard]] nff::gui::EditorHostPollResult pollLiveContent() override {
        ++pollCount_;
        const auto result = pollResult_;
        pollResult_.changed = false;
        return result;
    }

    [[nodiscard]] nff::gui::EditorHostSearchResult findText(
        const nff::gui::EditorHostSearchRequest& request) override {
        ++findCount_;
        lastSearchPattern_.assign(request.pattern);
        lastSearchStart_ = request.startOffset;
        lastSearchDirection_ = request.direction;
        return searchResult_;
    }

    [[nodiscard]] bool revealTextMatch(const nff::search::SearchMatch match,
                                       const std::string_view pattern) override {
        ++revealCount_;
        revealedMatch_ = match;
        revealedPattern_.assign(pattern);
        return revealResult_;
    }

    [[nodiscard]] bool replaceTextRange(
        const nff::search::SearchMatch match,
        const std::string_view replacement,
        const nff::metadata::TextColorEditPolicy colorPolicy) override {
        ++replaceRangeCount_;
        replacedMatch_ = match;
        replacementText_.assign(replacement);
        replacementColorPolicy_ = colorPolicy;
        return replaceRangeResult_;
    }

    [[nodiscard]] bool replaceAllText(
        const std::string_view text,
        const nff::metadata::TextColorEditPolicy colorPolicy) override {
        ++replaceAllCount_;
        replacementAllText_.assign(text);
        replacementAllColorPolicy_ = colorPolicy;
        return replaceAllResult_;
    }

    [[nodiscard]] std::error_code goToLine(const std::uint64_t oneBasedLine) override {
        ++goToCount_;
        lastGoToLine_ = oneBasedLine;
        return goToError_;
    }

    [[nodiscard]] std::error_code goToPosition(const std::uint64_t oneBasedLine,
                                               const std::uint64_t oneBasedColumn) override {
        ++goToPositionCount_;
        lastGoToLine_ = oneBasedLine;
        lastGoToColumn_ = oneBasedColumn;
        return goToError_;
    }

    [[nodiscard]] bool edit(const std::size_t offset,
                            const std::size_t eraseBytes,
                            const std::string& inserted) {
        return events_->applyTextEdit(
            view_, {offset, eraseBytes, inserted, binding_.documentRevision});
    }

    [[nodiscard]] bool restoreTextAppearanceRange(
        const std::uint64_t begin,
        const std::uint64_t end,
        const std::span<const nff::metadata::TextAppearanceSpan> spans) {
        return events_->restoreTextAppearanceRange(view_, begin, end, spans);
    }

    nff::workspace::ViewId view_{};
    nff::gui::IEditorHostEvents* events_{};
    nff::gui::EditorHostBinding binding_{};
    nff::gui::EditorHostRuntime runtime_{};
    nff::gui::Rect bounds_{};
    std::string boundText_;
    std::size_t bindCount_{0U};
    std::size_t visibleCallCount_{0U};
    std::size_t hiddenCallCount_{0U};
    bool restored_{false};
    bool visible_{false};
    bool focused_{false};
    nff::gui::EditorHostCommand lastCommand_{nff::gui::EditorHostCommand::Undo};
    std::size_t commandCount_{0U};
    std::size_t appearanceRefreshCount_{0U};
    std::size_t pollCount_{0U};
    nff::gui::EditorHostPollResult pollResult_{};
    std::size_t findCount_{0U};
    std::string lastSearchPattern_;
    std::uint64_t lastSearchStart_{0U};
    nff::search::SearchDirection lastSearchDirection_{nff::search::SearchDirection::Forward};
    nff::gui::EditorHostSearchResult searchResult_{};
    std::size_t revealCount_{0U};
    nff::search::SearchMatch revealedMatch_{};
    std::string revealedPattern_;
    bool revealResult_{true};
    std::size_t replaceRangeCount_{0U};
    nff::search::SearchMatch replacedMatch_{};
    std::string replacementText_;
    bool replaceRangeResult_{true};
    nff::metadata::TextColorEditPolicy replacementColorPolicy_{
        nff::metadata::TextColorEditPolicy::AdjustRanges};
    std::size_t replaceAllCount_{0U};
    std::string replacementAllText_;
    nff::metadata::TextColorEditPolicy replacementAllColorPolicy_{
        nff::metadata::TextColorEditPolicy::AdjustRanges};
    bool replaceAllResult_{true};
    std::size_t goToCount_{0U};
    std::size_t goToPositionCount_{0U};
    std::uint64_t lastGoToLine_{0U};
    std::uint64_t lastGoToColumn_{0U};
    std::error_code goToError_{};
    bool commandResult_{true};
};

class FakeFactory final : public nff::gui::IEditorHostFactory {
public:
    [[nodiscard]] std::unique_ptr<nff::gui::IEditorHost> create(
        const nff::workspace::ViewId view,
        nff::gui::IEditorHostEvents& events) override {
        auto host = std::make_unique<FakeHost>(view, events);
        instances_[view] = host.get();
        ++created_;
        return host;
    }

    [[nodiscard]] FakeHost* instance(const nff::workspace::ViewId view) const noexcept {
        const auto iterator = instances_.find(view);
        return iterator == instances_.end() ? nullptr : iterator->second;
    }

    std::map<nff::workspace::ViewId, FakeHost*> instances_;
    std::size_t created_{0U};
};

struct Fixture final {
    nff::core::DocumentManager documents;
    nff::workspace::WorkspaceModel workspace;
    nff::settings::AppSettings settings;
    nff::app::PresentationModel presentation{documents, workspace, settings};
    nff::gui::GuiShell shell{workspace, presentation};
    FakeFactory factory;
    nff::gui::GuiRuntime runtime{documents, workspace, presentation, shell, factory};
};

void testActiveViewOwnsVisibleHostAndRestoresViewState() {
    Fixture fixture;
    const auto document = fixture.documents.createUntitled();
    auto* value = fixture.documents.get(document);
    value->replaceText("hello\nworld\n");
    const auto view = fixture.workspace.openView(document, fixture.workspace.primaryPane());
    auto* persisted = fixture.workspace.view(view);
    persisted->caretOffset = 3U;
    persisted->anchorOffset = 1U;
    persisted->firstVisibleLine = 7U;
    persisted->fontFamilyOverride = "Consolas";
    persisted->fontPointSizeOverride = 15.0;

    fixture.shell.setViewport({1200.0, 800.0});
    static_cast<void>(fixture.runtime.synchronize());

    auto* host = fixture.factory.instance(view);
    expect(host != nullptr, "active tab creates editor host");
    expect(host != nullptr && host->visible_, "active host is visible");
    expect(host != nullptr && host->focused_, "global active host receives focus");
    expect(host != nullptr && host->restored_, "persisted view state restored on host creation");
    expect(host != nullptr && host->runtime_.caretOffset == 3U, "caret byte offset restored");
    expect(host != nullptr && host->binding_.preferredByteOffset == 3U,
           "persisted byte offset is carried into the first host binding");
    expect(host != nullptr && host->binding_.document == document, "document id bound to host");
    expect(host != nullptr && host->binding_.documentRevision == value->revision(),
           "document revision bound to host");
    expect(host != nullptr && host->binding_.fontFamily == "Consolas" &&
               host->binding_.fontPointSize == 15.0,
           "per-view font preferences bind to editor host");
    expect(host != nullptr && host->boundText_ == "hello\nworld\n", "document text bound to host");
    expect(host != nullptr && host->bounds_.width > 0.0 && host->bounds_.height > 0.0,
           "editor host receives pane content bounds");
}

void testFileBackedBindingCarriesPreparedInspectionMetadata() {
    Fixture fixture;
    const auto root = std::filesystem::temp_directory_path() / "nff-gui-runtime-prepared-binding";
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    std::filesystem::create_directories(root, cleanupError);
    expect(!cleanupError, "create prepared-binding fixture directory");
    const auto path = root / "prepared.txt";
    constexpr std::string_view content = "alpha\nbeta\n";
    const auto writeError = nff::storage::FileWriter::writeAtomically(path, bytes(content));
    expect(!writeError, "write prepared-binding fixture");

    const auto opened = fixture.documents.open(path);
    expect(static_cast<bool>(opened), "open prepared-binding document");
    const auto view = fixture.workspace.openView(opened.id, fixture.workspace.primaryPane());
    expect(static_cast<bool>(view), "open prepared-binding view");
    fixture.shell.setViewport({1200.0, 800.0});
    static_cast<void>(fixture.runtime.synchronize());

    auto* host = fixture.factory.instance(view);
    expect(host != nullptr && host->binding_.documentProfile != nullptr,
           "file-backed host receives inspected document profile pointer");
    expect(host != nullptr && host->binding_.documentFileState != nullptr &&
               host->binding_.documentFileState->exists &&
               host->binding_.documentFileState->size == content.size(),
           "file-backed host receives matching tracked file state pointer");

    std::filesystem::remove_all(root, cleanupError);
}

void testSplitCreatesOneVisibleHostPerPane() {
    Fixture fixture;
    const auto firstDocument = fixture.documents.createUntitled();
    const auto secondDocument = fixture.documents.createUntitled();
    const auto firstPane = fixture.workspace.primaryPane();
    const auto firstView = fixture.workspace.openView(firstDocument, firstPane);
    const auto split = fixture.workspace.splitPane(firstPane,
        nff::workspace::SplitOrientation::Horizontal);
    const auto secondView = fixture.workspace.openView(secondDocument, split.pane);
    static_cast<void>(fixture.workspace.setActivePane(split.pane));

    fixture.shell.setViewport({1400.0, 800.0});
    static_cast<void>(fixture.runtime.synchronize());

    const auto stats = fixture.runtime.stats();
    expect(stats.visibleHosts == 2U, "each split pane gets one visible editor host");
    expect(stats.residentHosts == 2U, "two active panes create two resident hosts");
    auto* first = fixture.factory.instance(firstView);
    auto* second = fixture.factory.instance(secondView);
    expect(first != nullptr && first->visible_, "first pane host stays visible");
    expect(second != nullptr && second->visible_, "second pane host visible");
    expect(first != nullptr && !first->focused_, "inactive pane host is not focused");
    expect(second != nullptr && second->focused_, "active pane host is focused");
}

void testStableActiveHostIsNotHiddenBetweenSynchronizations() {
    Fixture fixture;
    const auto document = fixture.documents.createUntitled();
    const auto view = fixture.workspace.openView(document, fixture.workspace.primaryPane());
    static_cast<void>(fixture.runtime.synchronize());
    auto* host = fixture.factory.instance(view);
    expect(host != nullptr, "stable visibility test host created");
    if (host == nullptr) {
        return;
    }

    const auto hiddenBefore = host->hiddenCallCount_;
    static_cast<void>(fixture.runtime.synchronize());
    expect(host->hiddenCallCount_ == hiddenBefore,
           "active editor host is not hidden during a no-op synchronization");
}

void testInactiveTabsUseBoundedDormantHostCache() {
    Fixture fixture;
    fixture.runtime.setPolicy({1U});
    const auto firstDocument = fixture.documents.createUntitled();
    const auto secondDocument = fixture.documents.createUntitled();
    const auto thirdDocument = fixture.documents.createUntitled();
    const auto pane = fixture.workspace.primaryPane();
    const auto firstView = fixture.workspace.openView(firstDocument, pane);
    const auto secondView = fixture.workspace.openView(secondDocument, pane);
    const auto thirdView = fixture.workspace.openView(thirdDocument, pane);

    expect(fixture.workspace.setActiveView(pane, firstView), "activate first view");
    static_cast<void>(fixture.runtime.synchronize());
    expect(fixture.workspace.setActiveView(pane, secondView), "activate second view");
    fixture.shell.requestRefresh();
    static_cast<void>(fixture.runtime.synchronize());
    expect(fixture.workspace.setActiveView(pane, thirdView), "activate third view");
    fixture.shell.requestRefresh();
    static_cast<void>(fixture.runtime.synchronize());

    const auto stats = fixture.runtime.stats();
    expect(stats.visibleHosts == 1U, "only active tab host is visible");
    expect(stats.dormantHosts <= 1U, "dormant editor hosts respect configured cap");
    expect(stats.residentHosts <= 2U, "host cache does not scale with open tab count");
}

void testRuntimeStateFlowsBackToWorkspaceAndPresentation() {
    Fixture fixture;
    const auto document = fixture.documents.createUntitled();
    const auto view = fixture.workspace.openView(document, fixture.workspace.primaryPane());
    static_cast<void>(fixture.runtime.synchronize());
    auto* host = fixture.factory.instance(view);
    expect(host != nullptr, "runtime state test host created");
    if (host == nullptr) {
        return;
    }

    host->runtime_.caretLine = 42U;
    host->runtime_.caretColumn = 9U;
    host->runtime_.selectionBytes = 12U;
    host->runtime_.contentBytes = 4096U;
    host->runtime_.windowByteStart = 1024U;
    host->runtime_.windowByteEnd = 2048U;
    host->runtime_.viewerCacheResidentBytes = 512U;
    host->runtime_.resolvedViewerPerformance = nff::viewer::PerformanceProfile::Fast;
    host->runtime_.caretOffset = 123U;
    host->runtime_.anchorOffset = 111U;
    host->runtime_.firstVisibleLine = 30U;
    host->runtime_.hasSelection = true;
    host->runtime_.canUndo = true;
    static_cast<void>(fixture.runtime.synchronize());

    const auto* persisted = fixture.workspace.view(view);
    const auto* runtime = fixture.presentation.viewRuntime(view);
    expect(persisted != nullptr && persisted->caretOffset == 123U,
           "host caret offset persisted into workspace view");
    expect(persisted != nullptr && persisted->firstVisibleLine == 30U,
           "host scroll position persisted into workspace view");
    expect(runtime != nullptr && runtime->caretLine == 42U && runtime->caretColumn == 9U,
           "host line and column reach presentation state");
    expect(runtime != nullptr && runtime->hasSelection && runtime->canUndo,
           "host command capabilities reach presentation state");
    expect(runtime != nullptr && runtime->contentBytes == 4096U &&
               runtime->windowByteStart == 1024U && runtime->windowByteEnd == 2048U,
           "scalable host byte telemetry reaches presentation state");
    expect(runtime != nullptr &&
               runtime->resolvedViewerPerformance == nff::viewer::PerformanceProfile::Fast &&
               runtime->viewerCacheResidentBytes == 512U,
           "viewer performance telemetry reaches presentation state");
    expect(fixture.runtime.synchronize().presentation.status.line == 42U,
           "global status bar sees active host runtime");
}

void testRevisionCheckedTextEditsSynchronizeAcrossViews() {
    Fixture fixture;
    const auto document = fixture.documents.createUntitled();
    auto* value = fixture.documents.get(document);
    value->replaceText("alpha beta");
    const auto firstPane = fixture.workspace.primaryPane();
    const auto firstView = fixture.workspace.openView(document, firstPane);
    const auto split = fixture.workspace.splitPane(firstPane,
        nff::workspace::SplitOrientation::Horizontal);
    const auto secondView = fixture.workspace.openView(document, split.pane);
    static_cast<void>(fixture.runtime.synchronize());

    auto* first = fixture.factory.instance(firstView);
    auto* second = fixture.factory.instance(secondView);
    expect(first != nullptr && second != nullptr, "same document can have two live editor hosts");
    if (first == nullptr || second == nullptr) {
        return;
    }

    const auto oldRevision = value->revision();
    expect(first->edit(6U, 4U, "gamma"), "revision-matched editor delta accepted");
    expect(value->text() == "alpha gamma", "editor delta mutates canonical document text");
    expect(value->revision() == oldRevision + 1U, "editor delta advances document revision");
    expect(!second->edit(0U, 5U, "stale"), "stale second-view delta is rejected");

    static_cast<void>(fixture.runtime.synchronize());
    expect(second->binding_.documentRevision == value->revision(),
           "second view receives fresh document revision on next synchronization");
    expect(second->boundText_ == "alpha gamma", "second view receives fresh canonical text");
}

void testRuntimeReportsAcceptedDocumentEdits() {
    nff::core::DocumentManager documents;
    nff::workspace::WorkspaceModel workspace;
    nff::settings::AppSettings settings;
    nff::app::PresentationModel presentation{documents, workspace, settings};
    nff::gui::GuiShell shell{workspace, presentation};
    FakeFactory factory;
    nff::core::DocumentId edited{};
    std::size_t editCount = 0U;
    nff::gui::GuiRuntime runtime{
        documents, workspace, presentation, shell, factory, {},
        {.documentWillEdit = {},
         .documentEdited = [&edited, &editCount](const nff::core::DocumentId document,
                                                 const nff::gui::EditorTextEdit&) {
             edited = document;
             ++editCount;
         },
         .appearance = {},
         .appearanceRangeRestored = {}}};

    const auto document = documents.createUntitled();
    auto* value = documents.get(document);
    value->replaceText("abc");
    const auto view = workspace.openView(document, workspace.primaryPane());
    static_cast<void>(runtime.synchronize());
    auto* host = factory.instance(view);
    expect(host != nullptr, "edit callback host created");
    if (host == nullptr) {
        return;
    }

    expect(host->edit(3U, 0U, "d"), "edit callback accepts canonical edit");
    expect(editCount == 1U && edited == document,
           "accepted edit reports owning document exactly once");
    expect(!host->edit(0U, 0U, "stale"), "stale edit rejected after revision advances");
    expect(editCount == 1U, "rejected edit does not report autosave notification");
}

void testRuntimeDecoratesBindingAndReportsEditBeforeAndAfter() {
    nff::core::DocumentManager documents;
    nff::workspace::WorkspaceModel workspace;
    nff::settings::AppSettings settings;
    nff::app::PresentationModel presentation{documents, workspace, settings};
    nff::gui::GuiShell shell{workspace, presentation};
    FakeFactory factory;
    bool willEdit = false;
    bool didEdit = false;
    nff::metadata::AppearanceStyle redStyle;
    redStyle.foregroundArgb = 0xFFFF0000U;
    nff::metadata::TextAppearanceSpan span{0U, 2U, redStyle};
    nff::gui::GuiRuntime runtime{
        documents, workspace, presentation, shell, factory, {},
        {.documentWillEdit = [&willEdit](const nff::core::DocumentId) { willEdit = true; },
         .documentEdited = [&didEdit](const nff::core::DocumentId,
                                     const nff::gui::EditorTextEdit&) { didEdit = true; },
         .appearance = [&span](const nff::core::DocumentId) {
             return nff::gui::EditorHostAppearance{
                 std::span<const nff::metadata::TextAppearanceSpan>(&span, 1U), {}, 7U, false};
         },
         .appearanceRangeRestored = {}}};

    const auto document = documents.createUntitled();
    auto* value = documents.get(document);
    value->replaceText("abc");
    const auto view = workspace.openView(document, workspace.primaryPane());
    static_cast<void>(runtime.synchronize());
    auto* host = factory.instance(view);
    expect(host != nullptr && host->binding_.appearanceRevision == 7U &&
               host->binding_.appearanceSpans.size() == 1U,
           "runtime binding decorator supplies selection appearance metadata to host");
    if (host == nullptr) {
        return;
    }
    expect(host->edit(1U, 0U, "x"), "decorated runtime accepts edit");
    expect(willEdit && didEdit, "runtime reports edit lifecycle around accepted mutation");
}

void testRuntimeRoutesAppearanceDeltaRestoreToOwningDocument() {
    nff::core::DocumentManager documents;
    nff::workspace::WorkspaceModel workspace;
    nff::settings::AppSettings settings;
    nff::app::PresentationModel presentation{documents, workspace, settings};
    nff::gui::GuiShell shell{workspace, presentation};
    FakeFactory factory;
    nff::core::DocumentId restoredDocument{};
    std::vector<nff::metadata::TextAppearanceSpan> restored;
    std::uint64_t restoredBegin = 0U;
    std::uint64_t restoredEnd = 0U;
    nff::gui::GuiRuntime runtime{
        documents, workspace, presentation, shell, factory, {},
        {.documentWillEdit = {},
         .documentEdited = {},
         .appearance = {},
         .appearanceRangeRestored =
             [&restoredDocument, &restored, &restoredBegin, &restoredEnd](
                 const nff::core::DocumentId document,
                 const std::uint64_t begin,
                 const std::uint64_t end,
                 const std::span<const nff::metadata::TextAppearanceSpan> spans) {
                 restoredDocument = document;
                 restoredBegin = begin;
                 restoredEnd = end;
                 restored.assign(spans.begin(), spans.end());
                 return true;
             }}};

    const auto document = documents.createUntitled();
    const auto view = workspace.openView(document, workspace.primaryPane());
    static_cast<void>(runtime.synchronize());
    auto* host = factory.instance(view);
    expect(host != nullptr, "text color restore host created");
    if (host == nullptr) {
        return;
    }

    nff::metadata::AppearanceStyle red;
    red.foregroundArgb = 0xFFFF0000U;
    nff::metadata::AppearanceStyle blue;
    blue.foregroundArgb = 0xFF0000FFU;
    const std::vector<nff::metadata::TextAppearanceSpan> fragment{
        {2U, 3U, red},
        {4U, 6U, blue},
    };
    expect(host->restoreTextAppearanceRange(2U, 6U, fragment),
           "host routes appearance delta restore through runtime");
    expect(restoredDocument == document && restoredBegin == 2U && restoredEnd == 6U && restored == fragment,
           "runtime restores only the affected appearance range on the owning document");
}

void testRuntimeCanInvalidateAllHostsForDocument() {
    Fixture fixture;
    const auto document = fixture.documents.createUntitled();
    const auto pane = fixture.workspace.primaryPane();
    const auto firstView = fixture.workspace.openView(document, pane);
    const auto split = fixture.workspace.splitPane(
        pane, nff::workspace::SplitOrientation::Horizontal);
    const auto secondView = fixture.workspace.openView(document, split.pane);
    static_cast<void>(fixture.runtime.synchronize());
    expect(fixture.runtime.host(firstView) != nullptr && fixture.runtime.host(secondView) != nullptr,
           "document invalidation test creates both hosts");

    fixture.runtime.invalidateDocument(document);
    expect(fixture.runtime.host(firstView) == nullptr && fixture.runtime.host(secondView) == nullptr,
           "document invalidation drops every resident host for document");
    static_cast<void>(fixture.runtime.synchronize());
    expect(fixture.runtime.host(firstView) != nullptr && fixture.runtime.host(secondView) != nullptr,
           "invalidated document hosts are recreated from canonical state");
}

void testRuntimeForwardsEditorCommandsAndAppearanceRefresh() {
    Fixture fixture;
    const auto document = fixture.documents.createUntitled();
    const auto view = fixture.workspace.openView(document, fixture.workspace.primaryPane());
    static_cast<void>(fixture.runtime.synchronize());
    auto* host = fixture.factory.instance(view);
    expect(host != nullptr, "command forwarding host created");
    if (host == nullptr) {
        return;
    }

    expect(fixture.runtime.executeEditorCommand(view, nff::gui::EditorHostCommand::SelectAll),
           "runtime forwards editor command to resident host");
    expect(host->commandCount_ == 1U &&
               host->lastCommand_ == nff::gui::EditorHostCommand::SelectAll,
           "editor command identity preserved");

    fixture.runtime.refreshHostAppearance();
    expect(host->appearanceRefreshCount_ == 1U,
           "appearance refresh reaches resident host");
}

void testRuntimePollsResidentLiveHostsAndHarvestsChanges() {
    Fixture fixture;
    const auto document = fixture.documents.createUntitled();
    const auto view = fixture.workspace.openView(document, fixture.workspace.primaryPane());
    fixture.presentation.sync();
    auto state = *fixture.presentation.viewRuntime(view);
    state.openMode = nff::core::OpenMode::Viewer;
    state.followEnabled = true;
    expect(fixture.presentation.setViewRuntime(view, state), "enable follow runtime state");
    static_cast<void>(fixture.runtime.synchronize());
    auto* host = fixture.factory.instance(view);
    expect(host != nullptr, "live poll host created");
    if (host == nullptr) {
        return;
    }
    expect(host->binding_.followEnabled, "follow state binds into native host");

    host->runtime_.contentBytes = 8192U;
    host->runtime_.windowByteStart = 4096U;
    host->runtime_.windowByteEnd = 8192U;
    host->runtime_.followWaiting = true;
    host->pollResult_.changed = true;
    const auto polled = fixture.runtime.pollLiveContent();
    expect(polled.changed && polled.changedHosts == 1U,
           "runtime reports changed live host");
    const auto* runtime = fixture.presentation.viewRuntime(view);
    expect(runtime != nullptr && runtime->contentBytes == 8192U &&
               runtime->windowByteEnd == 8192U && runtime->followWaiting,
           "live host telemetry is harvested after polling");
}

void testEditorSearchUsesCanonicalTextAndRevealsMatch() {
    Fixture fixture;
    const auto document = fixture.documents.createUntitled();
    auto* value = fixture.documents.get(document);
    value->replaceText("alpha beta alpha");
    const auto view = fixture.workspace.openView(document, fixture.workspace.primaryPane());
    static_cast<void>(fixture.runtime.synchronize());
    auto* host = fixture.factory.instance(view);
    expect(host != nullptr, "editor search host created");
    if (host == nullptr) {
        return;
    }

    nff::gui::EditorHostSearchRequest request;
    request.pattern = "alpha";
    request.startOffset = 6U;
    request.options.caseSensitive = true;
    const auto found = fixture.runtime.findText(view, request);
    expect(static_cast<bool>(found) && found.match.has_value(),
           "editor search finds canonical document text");
    expect(found.match.has_value() && found.match->offset == 11U,
           "editor search starts from requested byte offset");
    expect(host->findCount_ == 0U,
           "editor search does not delegate text scanning to native host");
    expect(host->revealCount_ == 1U && host->revealedMatch_.offset == 11U &&
               host->revealedPattern_ == "alpha",
           "editor search delegates only match reveal to native host");

    request.startOffset = 16U;
    const auto wrapped = fixture.runtime.findText(view, request);
    expect(static_cast<bool>(wrapped) && wrapped.match.has_value() && wrapped.wrapped,
           "editor search reports wrap-around");
    expect(wrapped.match.has_value() && wrapped.match->offset == 0U,
           "wrapped editor search returns first match");
}

void testViewerSearchDelegatesToScalableHost() {
    Fixture fixture;
    const auto document = fixture.documents.createUntitled();
    const auto view = fixture.workspace.openView(document, fixture.workspace.primaryPane());
    fixture.presentation.sync();
    auto state = *fixture.presentation.viewRuntime(view);
    state.openMode = nff::core::OpenMode::Viewer;
    expect(fixture.presentation.setViewRuntime(view, state), "set scalable viewer mode for search");
    static_cast<void>(fixture.runtime.synchronize());
    auto* host = fixture.factory.instance(view);
    expect(host != nullptr, "viewer search host created");
    if (host == nullptr) {
        return;
    }

    host->searchResult_.match = nff::search::SearchMatch{4096U, 5U};
    host->searchResult_.bytesScanned = 8192U;
    host->searchResult_.wrapped = true;
    nff::gui::EditorHostSearchRequest request;
    request.pattern = "ERROR";
    request.startOffset = 1024U;
    const auto found = fixture.runtime.findText(view, request);
    expect(static_cast<bool>(found) && found.match.has_value() &&
               found.match->offset == 4096U,
           "viewer search returns scalable host match");
    expect(found.bytesScanned == 8192U && found.wrapped,
           "viewer search preserves scalable telemetry");
    expect(host->findCount_ == 1U && host->lastSearchPattern_ == "ERROR" &&
               host->lastSearchStart_ == 1024U,
           "viewer search request is delegated without full-buffer scan");
    expect(host->revealCount_ == 1U,
           "viewer search reveals scalable host match");
}

void testEditorReplacementDelegatesToNativeHost() {
    Fixture fixture;
    const auto document = fixture.documents.createUntitled();
    const auto view = fixture.workspace.openView(document, fixture.workspace.primaryPane());
    static_cast<void>(fixture.runtime.synchronize());
    auto* host = fixture.factory.instance(view);
    expect(host != nullptr, "replacement delegation host created");
    if (host == nullptr) {
        return;
    }

    const nff::search::SearchMatch match{2U, 3U};
    expect(fixture.runtime.replaceTextRange(
               view, match, "XYZ", nff::metadata::TextColorEditPolicy::PreserveOffsets),
           "editor range replacement delegates to native host");
    expect(host->replaceRangeCount_ == 1U && host->replacedMatch_ == match &&
               host->replacementText_ == "XYZ" &&
               host->replacementColorPolicy_ ==
                   nff::metadata::TextColorEditPolicy::PreserveOffsets,
           "range replacement preserves match, bytes, and metadata policy");

    expect(fixture.runtime.replaceAllText(
               view, "replaced document",
               nff::metadata::TextColorEditPolicy::PreserveOffsets),
           "editor replace-all delegates to native host");
    expect(host->replaceAllCount_ == 1U && host->replacementAllText_ == "replaced document" &&
               host->replacementAllColorPolicy_ ==
                   nff::metadata::TextColorEditPolicy::PreserveOffsets,
           "replace-all preserves complete replacement text and metadata policy");

    fixture.presentation.sync();
    auto state = *fixture.presentation.viewRuntime(view);
    state.openMode = nff::core::OpenMode::Viewer;
    expect(fixture.presentation.setViewRuntime(view, state), "switch replacement test to viewer");
    expect(!fixture.runtime.replaceTextRange(view, match, "nope") &&
               !fixture.runtime.replaceAllText(view, "nope"),
           "read-only scalable viewer rejects replacement delegation");
}

void testBinarySearchRejectedAndGoToDelegated() {
    Fixture fixture;
    const auto document = fixture.documents.createUntitled();
    const auto view = fixture.workspace.openView(document, fixture.workspace.primaryPane());
    fixture.presentation.sync();
    auto state = *fixture.presentation.viewRuntime(view);
    state.openMode = nff::core::OpenMode::BinaryPreview;
    expect(fixture.presentation.setViewRuntime(view, state), "set binary mode for search test");
    static_cast<void>(fixture.runtime.synchronize());
    auto* host = fixture.factory.instance(view);
    expect(host != nullptr, "binary runtime host created");
    if (host == nullptr) {
        return;
    }

    nff::gui::EditorHostSearchRequest request;
    request.pattern = "AA";
    const auto found = fixture.runtime.findText(view, request);
    expect(found.error == std::errc::operation_not_supported,
           "binary preview rejects text search");
    expect(host->findCount_ == 0U && host->revealCount_ == 0U,
           "binary preview search never asks host to scan or reveal text");

    host->goToError_ = {};
    expect(!fixture.runtime.goToLine(view, 42U), "go-to line delegates to active content host");
    expect(host->goToCount_ == 1U && host->lastGoToLine_ == 42U,
           "go-to line preserves requested one-based line");
    expect(fixture.runtime.goToLine(view, 0U) == std::errc::invalid_argument,
           "go-to line rejects zero before host delegation");

    expect(!fixture.runtime.goToPosition(view, 12U, 7U),
           "go-to position delegates line and column to active content host");
    expect(host->goToPositionCount_ == 1U && host->lastGoToLine_ == 12U &&
               host->lastGoToColumn_ == 7U,
           "go-to position preserves one-based line and column");
    expect(fixture.runtime.goToPosition(view, 0U, 7U) == std::errc::invalid_argument &&
               fixture.runtime.goToPosition(view, 12U, 0U) == std::errc::invalid_argument,
           "go-to position rejects zero line or column before host delegation");
}

void testDocumentEditRejectsBrokenUtf8Boundaries() {
    nff::core::Document document;
    document.replaceText("A€B");
    const auto before = std::string(document.text());
    expect(static_cast<bool>(document.applyEdit(2U, 0U, "x")),
           "edit inside utf8 continuation byte rejected");
    expect(document.text() == before, "rejected utf8-boundary edit leaves text unchanged");
    const std::string invalid{static_cast<char>(0xC0), static_cast<char>(0x80)};
    expect(static_cast<bool>(document.applyEdit(1U, 0U, invalid)),
           "invalid inserted utf8 sequence rejected");
    expect(document.text() == before, "invalid inserted text leaves document unchanged");
}

}

int main() {
    testTextColorUndoJournalRestoresSnapshots();
    testTextAppearanceUndoJournalStoresAffectedRangeDeltas();
    testEditorHostBindingCarriesPreparedInspectionApi();
    testActiveViewOwnsVisibleHostAndRestoresViewState();
    testFileBackedBindingCarriesPreparedInspectionMetadata();
    testSplitCreatesOneVisibleHostPerPane();
    testStableActiveHostIsNotHiddenBetweenSynchronizations();
    testInactiveTabsUseBoundedDormantHostCache();
    testRuntimeStateFlowsBackToWorkspaceAndPresentation();
    testRevisionCheckedTextEditsSynchronizeAcrossViews();
    testRuntimeReportsAcceptedDocumentEdits();
    testRuntimeDecoratesBindingAndReportsEditBeforeAndAfter();
    testRuntimeRoutesAppearanceDeltaRestoreToOwningDocument();
    testRuntimeCanInvalidateAllHostsForDocument();
    testRuntimeForwardsEditorCommandsAndAppearanceRefresh();
    testRuntimePollsResidentLiveHostsAndHarvestsChanges();
    testEditorSearchUsesCanonicalTextAndRevealsMatch();
    testViewerSearchDelegatesToScalableHost();
    testEditorReplacementDelegatesToNativeHost();
    testBinarySearchRejectedAndGoToDelegated();
    testDocumentEditRejectsBrokenUtf8Boundaries();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "all gui runtime tests passed\n";
    return 0;
}
