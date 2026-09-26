#include "notepadFasaFiso/app/PresentationModel.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] const nff::app::CommandPresentation* command(
    const nff::app::ApplicationPresentationSnapshot& snapshot,
    const nff::app::CommandId id) {
    for (const auto& item : snapshot.commands) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

[[nodiscard]] std::filesystem::path makeTemporaryDirectory() {
    auto root = std::filesystem::temp_directory_path() / "nff-presentation-tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    return root;
}

void testWorkspaceProjectionAndCommands() {
    nff::core::DocumentManager documents;
    nff::workspace::WorkspaceModel workspace;
    nff::settings::AppSettings settings;

    const auto firstDocument = documents.createUntitled();
    auto* document = documents.get(firstDocument);
    expect(document != nullptr, "untitled document must exist");
    if (document == nullptr) {
        return;
    }
    document->replaceText("hello\nworld\n");

    const auto firstView = workspace.openView(firstDocument, workspace.primaryPane());
    expect(static_cast<bool>(firstView), "first view should open");
    if (auto* firstState = workspace.view(firstView)) {
        firstState->wordWrapOverride = false;
        firstState->lineNumbersOverride = true;
        firstState->fontFamilyOverride = "Consolas";
        firstState->fontPointSizeOverride = 15.0;
    }

    const auto split = workspace.splitPane(workspace.primaryPane(),
                                           nff::workspace::SplitOrientation::Vertical);
    expect(static_cast<bool>(split), "workspace should split");
    const auto secondView = workspace.openView(firstDocument, split.pane);
    expect(static_cast<bool>(secondView), "same document should open in second pane");

    nff::app::PresentationModel model(documents, workspace, settings);
    model.sync();

    const auto* firstRuntime = model.viewRuntime(firstView);
    expect(firstRuntime != nullptr && !firstRuntime->wordWrap &&
               firstRuntime->lineNumbersVisible,
           "per-view wrap and line-number overrides seed runtime");
    expect(firstRuntime != nullptr && firstRuntime->fontFamily == "Consolas" &&
               firstRuntime->fontPointSize == 15.0,
           "per-view font overrides seed runtime");

    nff::app::ViewRuntimeState runtime;
    runtime.openMode = nff::core::OpenMode::Editor;
    runtime.viewerPerformance = nff::viewer::PerformanceProfile::Fast;
    runtime.hasSelection = true;
    runtime.canUndo = true;
    runtime.canPaste = true;
    runtime.caretLine = 2U;
    runtime.caretColumn = 4U;
    runtime.selectionBytes = 5U;
    expect(model.setViewRuntime(secondView, runtime), "runtime state should bind to active view");

    const auto snapshot = model.snapshot();
    expect(snapshot.panes.size() == 2U, "presentation should expose two panes");
    expect(snapshot.activeView && *snapshot.activeView == secondView,
           "active view should follow workspace focus");
    expect(snapshot.activeDocument && *snapshot.activeDocument == firstDocument,
           "active document should be projected");
    expect(snapshot.splitRoot != nullptr &&
               snapshot.splitRoot->kind == nff::workspace::WorkspaceNode::Kind::Split,
           "split tree should be projected");
    expect(snapshot.status.visible, "status bar should be visible for active document");
    expect(snapshot.status.line == 2U && snapshot.status.column == 4U,
           "status bar should use runtime caret position");
    expect(snapshot.status.selectionBytes == 5U, "selection bytes should be projected");
    expect(snapshot.status.modified, "modified state should be projected");

    expect(snapshot.status.encoding == "UTF-8" && !snapshot.status.writesBom,
           "status projects current save encoding and BOM policy");
    const auto encodingError = document->setSaveEncoding(nff::encoding::Encoding::Utf16LE);
    expect(!encodingError, "presentation fixture changes save encoding");
    const auto bomError = document->setWritesBom(true);
    expect(!bomError, "presentation fixture enables BOM");
    const auto representationSnapshot = model.snapshot();
    expect(representationSnapshot.status.encoding == "UTF-16 LE" &&
               representationSnapshot.status.writesBom,
           "status immediately projects pending output representation");

    const auto* save = command(snapshot, nff::app::CommandId::Save);
    const auto* cut = command(snapshot, nff::app::CommandId::Cut);
    expect(save != nullptr && save->state.enabled, "Save should be enabled for modified editor");
    expect(cut != nullptr && cut->state.enabled, "Cut should be enabled with selection");

    runtime.openMode = nff::core::OpenMode::Viewer;
    runtime.followEnabled = true;
    expect(model.setViewRuntime(secondView, runtime), "viewer runtime should update");
    const auto viewerSnapshot = model.snapshot();
    const auto* viewerSave = command(viewerSnapshot, nff::app::CommandId::Save);
    const auto* fast = command(viewerSnapshot, nff::app::CommandId::ViewerPerformanceFast);
    expect(viewerSave != nullptr && !viewerSave->state.enabled,
           "Save should be disabled in viewer mode");
    expect(fast != nullptr && fast->state.enabled && fast->state.checked,
           "Fast viewer command should be checked");
    expect(viewerSnapshot.status.mode == "View", "status should expose viewer mode");
    expect(viewerSnapshot.status.viewerPerformance == "Fast",
           "status should expose viewer performance");
}

void testFormatCommandsMatchImplementedTransforms() {
    const auto root = makeTemporaryDirectory();
    const auto jsonPath = root / "sample.json";
    const auto xmlPath = root / "sample.xml";
    {
        std::ofstream(jsonPath, std::ios::binary | std::ios::trunc) << "{\"x\":1}\n";
        std::ofstream(xmlPath, std::ios::binary | std::ios::trunc) << "<root/>\n";
    }

    nff::core::DocumentManager documents;
    nff::workspace::WorkspaceModel workspace;
    nff::settings::AppSettings settings;
    nff::app::PresentationModel model(documents, workspace, settings);

    const auto json = documents.open(jsonPath);
    expect(static_cast<bool>(json), "JSON fixture opens");
    const auto jsonView = workspace.openView(json.id, workspace.primaryPane());
    expect(static_cast<bool>(jsonView), "JSON fixture view opens");
    model.sync();
    auto context = model.commandContext();
    expect(context.formatCanValidate && context.formatCanPrettyPrint && context.formatCanMinify,
           "JSON enables implemented format transforms");

    expect(workspace.closeView(jsonView), "JSON fixture view closes");
    const auto xml = documents.open(xmlPath);
    expect(static_cast<bool>(xml), "XML fixture opens");
    const auto xmlView = workspace.openView(xml.id, workspace.primaryPane());
    expect(static_cast<bool>(xmlView), "XML fixture view opens");
    model.sync();
    context = model.commandContext();
    expect(!context.formatCanValidate && !context.formatCanPrettyPrint && !context.formatCanMinify,
           "XML capabilities do not expose unimplemented JSON-only transforms");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void testExternalConflictProjectionIsExplicitAndCheap() {
    const auto root = makeTemporaryDirectory();
    const auto path = root / "external.txt";
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << "one\n";
    }

    nff::core::DocumentManager documents;
    const auto opened = documents.open(path);
    expect(static_cast<bool>(opened), "external conflict document opens");
    nff::workspace::WorkspaceModel workspace;
    const auto view = workspace.openView(opened.id, workspace.primaryPane());
    expect(static_cast<bool>(view), "external conflict view opens");
    nff::settings::AppSettings settings;
    nff::app::PresentationModel model(documents, workspace, settings);

    auto snapshot = model.snapshot();
    expect(!snapshot.status.externalConflict, "external conflict starts clear");
    model.setExternalConflict(opened.id, true);
    snapshot = model.snapshot();
    expect(snapshot.status.externalConflict, "status projects explicit external conflict");
    expect(snapshot.status.externalChangeState == nff::storage::FileChangeState::Modified,
           "status preserves external change kind for conflict UX");
    expect(!snapshot.status.explicitOverwriteRequired,
           "normal external change is distinct from recovered overwrite confirmation");
    expect(!snapshot.panes.empty() && !snapshot.panes.front().tabs.empty() &&
               snapshot.panes.front().tabs.front().externalConflict,
           "tab projects explicit external conflict");
    model.setExternalChangeState(opened.id, nff::storage::FileChangeState::Deleted);
    snapshot = model.snapshot();
    expect(snapshot.status.externalChangeState == nff::storage::FileChangeState::Deleted,
           "deleted state survives presentation projection");
    model.setExternalConflict(opened.id, false);
    snapshot = model.snapshot();
    expect(!snapshot.status.externalConflict, "external conflict can be cleared after reload/save");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void testRecoveredDocumentProjectionIsExplicit() {
    nff::core::DocumentManager documents;
    const auto document = documents.createUntitled();
    nff::workspace::WorkspaceModel workspace;
    const auto view = workspace.openView(document, workspace.primaryPane());
    expect(static_cast<bool>(view), "recovery projection view opens");
    nff::settings::AppSettings settings;
    nff::app::PresentationModel model(documents, workspace, settings);

    model.setRecovered(document, true);
    auto snapshot = model.snapshot();
    expect(snapshot.status.recovered, "status projects recovered document state");
    expect(!snapshot.status.externalConflict,
           "untitled recovery is distinct from an external conflict");
    expect(!snapshot.panes.empty() && !snapshot.panes.front().tabs.empty() &&
               snapshot.panes.front().tabs.front().recovered,
           "tab projects recovered document state");

    model.setRecovered(document, false);
    snapshot = model.snapshot();
    expect(!snapshot.status.recovered, "recovered state can be cleared after save/discard");
}

void testSidebarAndStaleRuntimeCleanup() {
    const auto root = makeTemporaryDirectory();
    {
        std::ofstream(root / "alpha-notes.txt") << "alpha";
        std::ofstream(root / "beta-log.txt") << "beta";
    }

    nff::core::DocumentManager documents;
    nff::workspace::WorkspaceModel workspace;
    nff::settings::AppSettings settings;
    nff::app::PresentationModel model(documents, workspace, settings);

    model.setSidebarWidth(20.0);
    auto snapshot = model.snapshot();
    expect(snapshot.sidebar.width == nff::app::PresentationModel::minimumSidebarWidth,
           "sidebar width should clamp to minimum");

    const auto document = documents.createUntitled();
    const auto view = workspace.openView(document, workspace.primaryPane());
    model.sync();
    expect(model.viewRuntime(view) != nullptr, "sync should create runtime state for views");

    expect(workspace.closeView(view), "view should close");
    model.sync();
    expect(model.viewRuntime(view) == nullptr, "sync should remove stale runtime state");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

}

int main() {
    testWorkspaceProjectionAndCommands();
    testSidebarAndStaleRuntimeCleanup();
    testExternalConflictProjectionIsExplicitAndCheap();
    testRecoveredDocumentProjectionIsExplicit();
    testFormatCommandsMatchImplementedTransforms();

    if (failures != 0) {
        std::cerr << failures << " presentation test(s) failed\n";
        return 1;
    }
    return 0;
}
