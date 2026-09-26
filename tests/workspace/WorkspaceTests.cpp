#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/core/SaveOptions.hpp"
#include "notepadFasaFiso/encoding/EncodingDetector.hpp"
#include "notepadFasaFiso/encoding/TextCodec.hpp"
#include "notepadFasaFiso/storage/FileReader.hpp"
#include "notepadFasaFiso/storage/FileWriter.hpp"
#include "notepadFasaFiso/workspace/WorkspaceModel.hpp"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <limits>
#include <random>
#include <span>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::vector<std::byte> bytes(const std::string_view text) {
    const auto* begin = reinterpret_cast<const std::byte*>(text.data());
    return {begin, begin + text.size()};
}

std::filesystem::path freshTempDirectory(const std::string_view name) {
    const auto path = std::filesystem::temp_directory_path() / std::string{name};
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path, error);
    return path;
}

void testDocumentManagerDeduplicatesOpenFiles() {
    const auto root = freshTempDirectory("nff-document-manager-tests");
    const auto path = root / "same.txt";
    const auto content = bytes("same document\n");
    auto error = nff::storage::FileWriter::writeAtomically(path, content);
    expect(!error, "write document manager fixture");

    nff::core::DocumentManager manager;
    const auto first = manager.open(path);
    const auto second = manager.open(root / "." / "same.txt");

    expect(static_cast<bool>(first), "first document open");
    expect(static_cast<bool>(second), "second document open");
    expect(!first.reusedExisting, "first open creates a document");
    expect(second.reusedExisting, "second open reuses the document");
    expect(first.id == second.id, "same file has one document identity");
    expect(manager.size() == 1, "deduplicated document count");

    const auto untitled = manager.createUntitled();
    expect(static_cast<bool>(untitled), "untitled document id");
    expect(manager.size() == 2, "untitled document registered");
    expect(manager.close(untitled), "close untitled document");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testDocumentManagerCreatesFileBackedUnsavedTarget() {
    const auto root = freshTempDirectory("nff-document-manager-new-file-tests");
    const auto path = root / "future-note.txt";

    nff::core::DocumentManager manager;
    const auto created = manager.createNewFile(path);
    expect(static_cast<bool>(created), "create nonexistent file-backed document");
    expect(!std::filesystem::exists(path), "new file target is not created before save");

    auto* document = manager.get(created.id);
    expect(document != nullptr && document->path() == path,
           "new file target adopts requested path without disk write");
    expect(document != nullptr && document->text().empty() && !document->modified(),
           "new file target starts as clean empty editor buffer");
    if (document != nullptr) {
        document->replaceText("created later\n");
        expect(!document->save(), "saving new file target creates requested file");
    }
    expect(std::filesystem::exists(path), "new file target appears on disk only after save");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testRecursiveWorkspaceSplitsAndSharedDocumentViews() {
    nff::core::DocumentManager manager;
    const auto documentA = manager.createUntitled();
    const auto documentB = manager.createUntitled();

    nff::workspace::WorkspaceModel workspace;
    const auto paneA = workspace.primaryPane();
    const auto viewA = workspace.openView(documentA, paneA);
    expect(static_cast<bool>(viewA), "open first workspace view");

    const auto splitB = workspace.splitPane(
        paneA, nff::workspace::SplitOrientation::Horizontal,
        nff::workspace::SplitPlacement::After, 0.6);
    expect(static_cast<bool>(splitB), "horizontal split");
    expect(workspace.setSplitRatio(splitB.split, 0.55), "adjust split ratio");

    const auto secondViewOfA = workspace.openView(documentA, splitB.pane);
    expect(static_cast<bool>(secondViewOfA), "same document can have another view");
    expect(secondViewOfA != viewA, "views have independent identity");
    expect(workspace.view(secondViewOfA)->document == documentA,
           "second view references shared document");

    const auto containingPane = workspace.paneContaining(secondViewOfA);
    expect(containingPane && *containingPane == splitB.pane,
           "workspace resolves containing pane without snapshot allocation");

    const auto splitC = workspace.splitPane(
        splitB.pane, nff::workspace::SplitOrientation::Vertical,
        nff::workspace::SplitPlacement::Before, 0.4);
    expect(static_cast<bool>(splitC), "nested vertical split");
    const auto viewB = workspace.openView(documentB, splitC.pane);
    expect(static_cast<bool>(viewB), "open view in nested pane");
    expect(workspace.paneCount() == 3, "recursive split creates three panes");
    expect(workspace.viewCount() == 3, "workspace tracks independent views");

    expect(workspace.moveView(viewA, splitC.pane, 0), "move tab between panes");
    const auto* movedPane = workspace.pane(splitC.pane);
    expect(movedPane != nullptr && !movedPane->views.empty() && movedPane->views.front() == viewA,
           "moved tab honors target index");
    expect(workspace.moveView(viewA, splitC.pane, 2), "reorder tab within pane");
    movedPane = workspace.pane(splitC.pane);
    expect(movedPane != nullptr && movedPane->views.size() == 2U &&
               movedPane->views.back() == viewA,
           "same-pane tab reorder honors insertion index");
    expect(workspace.setActiveView(splitC.pane, viewB), "activate a view in pane");

    expect(!workspace.removeEmptyPane(splitC.pane), "non-empty pane cannot be removed");
    expect(workspace.closeView(viewA), "close moved view");
    expect(workspace.closeView(viewB), "close nested view");
    expect(workspace.removeEmptyPane(splitC.pane), "empty nested pane can be removed");
    expect(workspace.paneCount() == 2, "pane tree collapses after removal");

    expect(!workspace.setSplitRatio(splitB.split, 0.99), "reject unusable split ratio");
}

void testAlignedTwoByTwoNormalizesToIndependentRows() {
    nff::core::DocumentManager manager;
    const auto documentA = manager.createUntitled();
    const auto documentB = manager.createUntitled();
    const auto documentC = manager.createUntitled();
    const auto documentD = manager.createUntitled();

    nff::workspace::WorkspaceModel workspace;
    const auto paneA = workspace.primaryPane();
    const auto rootSplit = workspace.splitPane(
        paneA, nff::workspace::SplitOrientation::Horizontal,
        nff::workspace::SplitPlacement::After, 0.5);
    const auto paneB = rootSplit.pane;
    const auto leftRows = workspace.splitPane(
        paneA, nff::workspace::SplitOrientation::Vertical,
        nff::workspace::SplitPlacement::After, 0.5);
    const auto paneC = leftRows.pane;
    const auto rightRows = workspace.splitPane(
        paneB, nff::workspace::SplitOrientation::Vertical,
        nff::workspace::SplitPlacement::After, 0.5);
    const auto paneD = rightRows.pane;

    const auto viewA = workspace.openView(documentA, paneA);
    const auto viewB = workspace.openView(documentB, paneB);
    const auto viewC = workspace.openView(documentC, paneC);
    const auto viewD = workspace.openView(documentD, paneD);

    expect(workspace.normalizeAlignedTwoByTwoRows(),
           "aligned column-major 2x2 normalizes to row-major topology");

    const auto& root = workspace.root();
    expect(root.kind() == nff::workspace::WorkspaceNode::Kind::Split &&
               root.orientation() == nff::workspace::SplitOrientation::Vertical,
           "normalized 2x2 root becomes top-bottom split");
    expect(root.first() != nullptr && root.second() != nullptr &&
               root.first()->kind() == nff::workspace::WorkspaceNode::Kind::Split &&
               root.second()->kind() == nff::workspace::WorkspaceNode::Kind::Split &&
               root.first()->orientation() == nff::workspace::SplitOrientation::Horizontal &&
               root.second()->orientation() == nff::workspace::SplitOrientation::Horizontal,
           "normalized 2x2 gives each row its own left-right split");

    const auto* top = root.first();
    const auto* bottom = root.second();
    expect(top != nullptr && top->first() != nullptr && top->second() != nullptr &&
               top->first()->pane() == paneA && top->second()->pane() == paneB,
           "normalized top row preserves left and right pane identities");
    expect(bottom != nullptr && bottom->first() != nullptr && bottom->second() != nullptr &&
               bottom->first()->pane() == paneC && bottom->second()->pane() == paneD,
           "normalized bottom row preserves left and right pane identities");

    expect(workspace.paneContaining(viewA) == paneA && workspace.paneContaining(viewB) == paneB &&
               workspace.paneContaining(viewC) == paneC && workspace.paneContaining(viewD) == paneD,
           "normalization preserves existing view ownership");

    const auto topSplit = top == nullptr ? nff::workspace::SplitId{} : top->split();
    const auto bottomSplit = bottom == nullptr ? nff::workspace::SplitId{} : bottom->split();
    expect(static_cast<bool>(topSplit) && static_cast<bool>(bottomSplit) && topSplit != bottomSplit,
           "rows own distinct split identities");
    expect(workspace.setSplitRatio(topSplit, 0.35), "adjust normalized top row ratio");
    expect(workspace.root().first() != nullptr && workspace.root().second() != nullptr &&
               workspace.root().first()->ratio() == 0.35 &&
               workspace.root().second()->ratio() == 0.5,
           "top row ratio changes without moving bottom row boundary");

    const auto snapshot = workspace.snapshot();
    nff::workspace::WorkspaceModel restored;
    expect(restored.restore(snapshot), "restore normalized 2x2 snapshot");
    expect(restored.root().orientation() == nff::workspace::SplitOrientation::Vertical &&
               restored.root().first() != nullptr && restored.root().second() != nullptr &&
               restored.root().first()->ratio() == 0.35 &&
               restored.root().second()->ratio() == 0.5,
           "session snapshot preserves independent row ratios");
}

void testAlignedTwoByTwoNormalizesToIndependentColumns() {
    nff::workspace::WorkspaceModel workspace;
    const auto paneA = workspace.primaryPane();
    const auto top = workspace.splitPane(
        paneA, nff::workspace::SplitOrientation::Horizontal,
        nff::workspace::SplitPlacement::After, 0.5);
    const auto paneB = top.pane;
    const auto lowerLeft = workspace.splitPane(
        paneA, nff::workspace::SplitOrientation::Vertical,
        nff::workspace::SplitPlacement::After, 0.5);
    const auto paneC = lowerLeft.pane;
    const auto lowerRight = workspace.splitPane(
        paneB, nff::workspace::SplitOrientation::Vertical,
        nff::workspace::SplitPlacement::After, 0.5);
    const auto paneD = lowerRight.pane;

    expect(workspace.normalizeAlignedTwoByTwoRows(),
           "prepare aligned row-major 2x2 before reciprocal pivot");
    expect(workspace.normalizeAlignedTwoByTwoColumns(),
           "aligned row-major 2x2 normalizes back to column-major topology");

    const auto& root = workspace.root();
    expect(root.orientation() == nff::workspace::SplitOrientation::Horizontal &&
               root.first() != nullptr && root.second() != nullptr &&
               root.first()->orientation() == nff::workspace::SplitOrientation::Vertical &&
               root.second()->orientation() == nff::workspace::SplitOrientation::Vertical,
           "reciprocal pivot gives each column its own top-bottom splitter");
    expect(root.first()->first()->pane() == paneA && root.first()->second()->pane() == paneC &&
               root.second()->first()->pane() == paneB && root.second()->second()->pane() == paneD,
           "reciprocal pivot preserves pane positions");

    const auto leftSplit = root.first()->split();
    const auto rightSplit = root.second()->split();
    expect(leftSplit != rightSplit, "columns own distinct split identities");
    expect(workspace.setSplitRatio(leftSplit, 0.35), "resize only left column top-bottom ratio");
    expect(workspace.root().first()->ratio() == 0.35 &&
               workspace.root().second()->ratio() == 0.5,
           "left column vertical resize does not move right column boundary");
}

void testUnalignedTwoByTwoDoesNotNormalize() {
    nff::workspace::WorkspaceModel workspace;
    const auto paneA = workspace.primaryPane();
    const auto rootSplit = workspace.splitPane(
        paneA, nff::workspace::SplitOrientation::Horizontal,
        nff::workspace::SplitPlacement::After, 0.5);
    const auto paneB = rootSplit.pane;
    static_cast<void>(workspace.splitPane(
        paneA, nff::workspace::SplitOrientation::Vertical,
        nff::workspace::SplitPlacement::After, 0.35));
    static_cast<void>(workspace.splitPane(
        paneB, nff::workspace::SplitOrientation::Vertical,
        nff::workspace::SplitPlacement::After, 0.65));

    expect(!workspace.normalizeAlignedTwoByTwoRows(),
           "misaligned column row boundaries do not reshape implicitly");
    expect(workspace.root().orientation() == nff::workspace::SplitOrientation::Horizontal,
           "misaligned 2x2 keeps original column-major root");
}

void testSaveAsSaveCopyAndLineEndingPolicy() {
    const auto root = freshTempDirectory("nff-save-policy-tests");
    const auto originalPath = root / "original.txt";
    const auto copyPath = root / "copy.any-extension";
    const auto adoptedPath = root / "README.custom";

    auto initial = bytes("one\r\ntwo\r\n");
    auto error = nff::storage::FileWriter::writeAtomically(originalPath, initial);
    expect(!error, "write save policy fixture");

    nff::core::Document document;
    error = document.load(originalPath);
    expect(!error, "load save policy fixture");
    document.replaceText("Türkçe\r\nsecond\nthird\r");

    nff::core::SaveOptions copyOptions;
    copyOptions.encoding = nff::encoding::Encoding::Utf16LE;
    copyOptions.writeBom = true;
    copyOptions.lineEnding = nff::core::LineEndingPolicy::LF;

    error = document.saveCopy(copyPath, copyOptions);
    expect(!error, "save copy with independent encoding policy");
    expect(document.path() == originalPath, "save copy does not adopt destination path");
    expect(document.modified(), "save copy does not mark source document clean");
    expect(document.saveEncoding() == nff::encoding::Encoding::Utf8,
           "save copy does not alter source encoding policy");

    const auto copyRead = nff::storage::FileReader::readAll(copyPath, 4096);
    expect(static_cast<bool>(copyRead), "read saved copy");
    if (copyRead) {
        const auto detection = nff::encoding::EncodingDetector::detect(copyRead.bytes);
        expect(detection.encoding == nff::encoding::Encoding::Utf16LE,
               "saved copy uses requested UTF-16 encoding");
        expect(detection.hasBom, "saved copy writes requested BOM");
        const auto decoded = nff::encoding::TextCodec::decode(copyRead.bytes, detection);
        expect(static_cast<bool>(decoded), "decode saved copy");
        if (decoded) {
            expect(decoded.text == "Türkçe\nsecond\nthird\n",
                   "save copy normalizes line endings without mutating source");
        }
    }
    expect(document.text() == "Türkçe\r\nsecond\nthird\r",
           "save copy leaves canonical source text untouched");

    nff::core::SaveOptions saveAsOptions;
    saveAsOptions.encoding = nff::encoding::Encoding::Utf8;
    saveAsOptions.writeBom = false;
    saveAsOptions.lineEnding = nff::core::LineEndingPolicy::CRLF;
    error = document.saveAs(adoptedPath, saveAsOptions);
    expect(!error, "save as arbitrary extension");
    expect(document.path() == adoptedPath, "save as adopts exact requested filename");
    expect(document.text() == "Türkçe\r\nsecond\r\nthird\r\n",
           "save as commits explicit line-ending conversion");
    expect(!document.modified(), "save as marks current document clean");
    expect(document.profile().lineEnding == nff::core::LineEnding::CRLF,
           "save as refreshes document profile");

    nff::core::SaveOptions invalidOptions;
    invalidOptions.encoding = nff::encoding::Encoding::Windows1254;
    invalidOptions.writeBom = true;
    error = document.saveCopy(root / "invalid.txt", invalidOptions);
    expect(error == std::make_error_code(std::errc::invalid_argument),
           "legacy encodings reject impossible BOM requests");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testWorkspaceMoveKeepsStableActiveFallback() {
    nff::core::DocumentManager manager;
    const auto documentA = manager.createUntitled();
    const auto documentB = manager.createUntitled();
    const auto documentC = manager.createUntitled();

    nff::workspace::WorkspaceModel workspace;
    const auto sourcePane = workspace.primaryPane();
    const auto viewA = workspace.openView(documentA, sourcePane);
    const auto viewB = workspace.openView(documentB, sourcePane);
    const auto viewC = workspace.openView(documentC, sourcePane);
    const auto split = workspace.splitPane(
        sourcePane, nff::workspace::SplitOrientation::Horizontal);

    expect(static_cast<bool>(viewA) && static_cast<bool>(viewB) && static_cast<bool>(viewC) &&
               static_cast<bool>(split),
           "create stable-active move fixture");
    expect(workspace.setActiveView(sourcePane, viewA), "activate first source tab");
    expect(workspace.moveView(viewA, split.pane), "move active first tab to another pane");

    const auto* source = workspace.pane(sourcePane);
    expect(source != nullptr && source->views.size() == 2U && source->views[0] == viewB &&
               source->views[1] == viewC,
           "cross-pane move removes the view from its source exactly once");
    expect(source != nullptr && source->activeView == viewB,
           "source pane activates the nearest surviving tab after moving its active first tab");
    const auto* target = workspace.pane(split.pane);
    expect(target != nullptr && target->views.size() == 1U && target->views.front() == viewA,
           "cross-pane move gives the view one target-pane owner");
}

void testClosingActiveTabSelectsNearestSurvivor() {
    nff::core::DocumentManager manager;
    const auto documentA = manager.createUntitled();
    const auto documentB = manager.createUntitled();
    const auto documentC = manager.createUntitled();
    const auto documentD = manager.createUntitled();

    nff::workspace::WorkspaceModel workspace;
    const auto pane = workspace.primaryPane();
    const auto viewA = workspace.openView(documentA, pane);
    const auto viewB = workspace.openView(documentB, pane);
    const auto viewC = workspace.openView(documentC, pane);
    const auto viewD = workspace.openView(documentD, pane);

    expect(workspace.setActiveView(pane, viewB), "activate middle tab before close");
    expect(workspace.closeView(viewB), "close active middle tab");
    const auto* state = workspace.pane(pane);
    expect(state != nullptr && state->activeView == viewC,
           "closing a middle tab activates the tab that slides into the same slot");

    expect(workspace.setActiveView(pane, viewD), "activate last tab before close");
    expect(workspace.closeView(viewD), "close active last tab");
    state = workspace.pane(pane);
    expect(state != nullptr && state->activeView == viewC,
           "closing the last tab activates its immediate left neighbor");

    expect(state != nullptr && state->views.size() == 2U &&
               state->views[0] == viewA && state->views[1] == viewC,
           "tab close preserves surviving tab order");
}

void testWorkspaceAllocationFreeTraversal() {
    nff::core::DocumentManager documents;
    const auto first = documents.createUntitled();
    const auto second = documents.createUntitled();
    nff::workspace::WorkspaceModel workspace;
    const auto pane = workspace.primaryPane();
    static_cast<void>(workspace.openView(first, pane));
    const auto split = workspace.splitPane(pane, nff::workspace::SplitOrientation::Horizontal);
    expect(static_cast<bool>(split), "traversal fixture split created");
    if (split) static_cast<void>(workspace.openView(second, split.pane));
    std::size_t paneVisits = 0U;
    std::size_t viewVisits = 0U;
    workspace.forEachPane([&](const nff::workspace::PaneState&) { ++paneVisits; });
    workspace.forEachView([&](const nff::workspace::ViewState&) { ++viewVisits; });
    expect(paneVisits == workspace.paneCount(), "forEachPane visits each pane once");
    expect(viewVisits == workspace.viewCount(), "forEachView visits each view once");
}

void testWorkspaceSameSlotMoveIsNoOp() {
    nff::core::DocumentManager manager;
    const auto documentA = manager.createUntitled();
    const auto documentB = manager.createUntitled();
    const auto documentC = manager.createUntitled();

    nff::workspace::WorkspaceModel workspace;
    const auto pane = workspace.primaryPane();
    const auto viewA = workspace.openView(documentA, pane);
    const auto viewB = workspace.openView(documentB, pane);
    const auto viewC = workspace.openView(documentC, pane);

    expect(workspace.setActiveView(pane, viewC), "activate third tab before no-op move");
    expect(!workspace.moveView(viewB, pane, 1U), "same-slot tab move reports no mutation");

    const auto* state = workspace.pane(pane);
    expect(state != nullptr && state->views.size() == 3U && state->views[0] == viewA &&
               state->views[1] == viewB && state->views[2] == viewC,
           "same-slot move preserves tab ordering");
    expect(state != nullptr && state->activeView == viewC,
           "same-slot move preserves the active tab");

    expect(!workspace.moveView(viewA, pane),
           "moving a tab to its current pane without an index is a no-op");
    state = workspace.pane(pane);
    expect(state != nullptr && state->views.size() == 3U && state->views[0] == viewA &&
               state->views[1] == viewB && state->views[2] == viewC,
           "same-pane content drop preserves tab ordering");
}

}

void testOwnershipMutationsAndExhaustion() {
    using namespace nff::workspace;
    WorkspaceModel model;
    std::mt19937 random(0x4e4646U);
    std::vector<PaneId> panes{model.primaryPane()};
    std::vector<ViewId> views;
    for (unsigned int i = 0; i < 2000; ++i) {
        const auto choice = random() % 5U;
        if (choice == 0 && panes.size() < 24) {
            panes.push_back(model.splitPane(panes[random() % panes.size()], SplitOrientation::Horizontal).pane);
        } else if (choice == 1 || views.empty()) {
            views.push_back(model.openView(nff::core::DocumentId{1}, panes[random() % panes.size()]));
        } else if (choice == 2) {
            static_cast<void>(model.moveView(views[random() % views.size()], panes[random() % panes.size()], random() % 8U));
        } else if (choice == 3) {
            const auto index = random() % views.size();
            const auto closed = views[index];
            expect(model.closeView(closed), "close randomized view");
            expect(!model.paneContaining(closed), "closed view has no owner");
            views.erase(views.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            const auto snapshot = model.snapshot();
            expect(model.restore(snapshot), "restore randomized workspace");
        }
        const auto snapshot = model.snapshot();
        for (const auto& pane : snapshot.panes) for (auto view : pane.views) {
            expect(model.paneContaining(view) == pane.id, "ownership matches pane after mutation");
        }
        expect(model.viewCount() == views.size(), "randomized view count");
    }
    auto snapshot = model.snapshot();
    snapshot.nextViewId = std::numeric_limits<std::uint64_t>::max();
    snapshot.nextPaneId = std::numeric_limits<std::uint64_t>::max();
    snapshot.nextSplitId = std::numeric_limits<std::uint64_t>::max();
    expect(model.restore(snapshot), "restore exhausted ID state");
    expect(!model.openView(nff::core::DocumentId{1}, model.primaryPane()), "view IDs never wrap");
    expect(!model.splitPane(model.primaryPane(), SplitOrientation::Vertical), "pane and split IDs never wrap");
    expect(model.restore(model.snapshot()), "exhaustion preserves valid snapshot");
}

int main() {
    testOwnershipMutationsAndExhaustion();
    testDocumentManagerDeduplicatesOpenFiles();
    testDocumentManagerCreatesFileBackedUnsavedTarget();
    testRecursiveWorkspaceSplitsAndSharedDocumentViews();
    testAlignedTwoByTwoNormalizesToIndependentRows();
    testAlignedTwoByTwoNormalizesToIndependentColumns();
    testUnalignedTwoByTwoDoesNotNormalize();
    testWorkspaceMoveKeepsStableActiveFallback();
    testWorkspaceAllocationFreeTraversal();
    testClosingActiveTabSelectsNearestSurvivor();
    testWorkspaceSameSlotMoveIsNoOp();
    testSaveAsSaveCopyAndLineEndingPolicy();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "all workspace tests passed\n";
    return 0;
}
