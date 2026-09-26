#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/core/SaveOptions.hpp"
#include "notepadFasaFiso/recovery/RecoveryManager.hpp"
#include "notepadFasaFiso/session/SessionRestorer.hpp"
#include "notepadFasaFiso/session/SessionStore.hpp"
#include "notepadFasaFiso/storage/FileWriter.hpp"
#include "notepadFasaFiso/workspace/WorkspaceModel.hpp"

#include <cstddef>
#include <filesystem>
#include <iostream>
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

const nff::session::RestoredDocument* restoredFor(
    const nff::session::SessionRestoreResult& result,
    const nff::core::DocumentId persisted) {
    for (const auto& document : result.documents) {
        if (document.persistedId == persisted) {
            return &document;
        }
    }
    return nullptr;
}

void testRecoveryRestoreAndDocumentRemap() {
    const auto root = freshTempDirectory("nff-session-restore-tests");
    const auto file = root / "named.txt";
    auto error = nff::storage::FileWriter::writeAtomically(file, bytes("disk version\n"));
    expect(!error, "write named restore fixture");

    nff::core::DocumentManager sourceDocuments;
    const auto named = sourceDocuments.open(file);
    expect(static_cast<bool>(named), "open named source document");
    const auto untitled = sourceDocuments.createUntitled();
    sourceDocuments.get(named.id)->replaceText("recovered named\n");
    sourceDocuments.get(untitled)->replaceText("recovered scratch\n");

    nff::workspace::WorkspaceModel sourceWorkspace;
    const auto firstView = sourceWorkspace.openView(named.id, sourceWorkspace.primaryPane());
    expect(static_cast<bool>(firstView), "open named source view");
    const auto split = sourceWorkspace.splitPane(sourceWorkspace.primaryPane(),
                                                 nff::workspace::SplitOrientation::Vertical);
    expect(static_cast<bool>(split), "split source workspace");
    const auto secondView = sourceWorkspace.openView(untitled, split.pane);
    expect(static_cast<bool>(secondView), "open scratch source view");

    nff::recovery::RecoveryManager recovery(root / "recovery", 77U);
    nff::metadata::TextAppearanceMap namedAppearance;
    namedAppearance.setForeground(0U, 9U, 0xFF22AA66U);
    error = recovery.checkpoint(
        named.id, *sourceDocuments.get(named.id), &namedAppearance);
    expect(!error, "checkpoint named document");
    error = recovery.checkpoint(untitled, *sourceDocuments.get(untitled));
    expect(!error, "checkpoint untitled document");

    const auto state = nff::session::SessionStore::capture(sourceDocuments,
                                                           sourceWorkspace,
                                                           &recovery);

    nff::core::DocumentManager targetDocuments;
    const auto preexisting = targetDocuments.createUntitled();
    expect(static_cast<bool>(preexisting), "reserve runtime document id");
    nff::workspace::WorkspaceModel targetWorkspace;

    const auto restored = nff::session::SessionRestorer::restore(state,
                                                                  targetDocuments,
                                                                  targetWorkspace,
                                                                  &recovery);
    expect(restored.workspaceRestored, "workspace restores with remapped document ids");
    expect(!restored.usedFallbackWorkspace, "valid workspace does not use fallback");
    expect(restored.restoredDocumentCount() == 2U, "both session documents restore");
    expect(targetWorkspace.paneCount() == 2U, "split layout survives restore");
    expect(targetWorkspace.viewCount() == 2U, "both views survive restore");

    const auto* namedRestored = restoredFor(restored, named.id);
    const auto* untitledRestored = restoredFor(restored, untitled);
    expect(namedRestored != nullptr && namedRestored->runtimeId != named.id,
           "persisted ids remap to runtime ids");
    expect(untitledRestored != nullptr, "untitled mapping exists");

    if (namedRestored != nullptr) {
        expect(namedRestored->recoveredAppearance.has_value(),
               "named recovery transports selection appearance");
        if (namedRestored->recoveredAppearance.has_value()) {
            expect(namedRestored->recoveredAppearance->spans() == namedAppearance.spans(),
                   "named recovery appearance survives session restore");
        }
        auto* document = targetDocuments.get(namedRestored->runtimeId);
        expect(document != nullptr && document->text() == "recovered named\n",
               "named recovery content wins over disk content");
        expect(document != nullptr && document->requiresExplicitOverwrite(),
               "recovered named document blocks silent overwrite");
        if (document != nullptr) {
            error = document->save();
            expect(error == std::make_error_code(std::errc::text_file_busy),
                   "normal save refuses recovered overwrite");
            nff::core::SaveOptions overwrite;
            overwrite.allowExternalOverwrite = true;
            error = document->save(overwrite);
            expect(!error, "explicit overwrite saves recovered named document");
            expect(!document->requiresExplicitOverwrite(),
                   "explicit save clears recovery overwrite guard");
        }
    }

    if (untitledRestored != nullptr) {
        const auto* document = targetDocuments.get(untitledRestored->runtimeId);
        expect(document != nullptr && document->text() == "recovered scratch\n",
               "untitled recovery content restores");
        expect(document != nullptr && document->path().empty(),
               "untitled recovery remains untitled");
        expect(document != nullptr && !document->requiresExplicitOverwrite(),
               "untitled recovery does not require overwrite permission");
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testBidirectionalWorkspaceRoundTripPreservesTopologyAndViewIdentity() {
    const auto root = freshTempDirectory("nff-session-bidirectional-workspace-tests");
    const auto sharedPath = root / "shared.txt";
    const auto alternatePath = root / "alternate.txt";
    auto error = nff::storage::FileWriter::writeAtomically(sharedPath, bytes("shared\n"));
    expect(!error, "write shared workspace fixture");
    error = nff::storage::FileWriter::writeAtomically(alternatePath, bytes("alternate\n"));
    expect(!error, "write alternate workspace fixture");

    nff::core::DocumentManager sourceDocuments;
    const auto shared = sourceDocuments.open(sharedPath);
    const auto alternate = sourceDocuments.open(alternatePath);
    expect(static_cast<bool>(shared) && static_cast<bool>(alternate),
           "open workspace round-trip documents");

    nff::workspace::WorkspaceModel sourceWorkspace;
    const auto paneA = sourceWorkspace.primaryPane();
    const auto right = sourceWorkspace.splitPane(
        paneA, nff::workspace::SplitOrientation::Horizontal,
        nff::workspace::SplitPlacement::After, 0.5);
    const auto paneB = right.pane;
    const auto lowerLeft = sourceWorkspace.splitPane(
        paneA, nff::workspace::SplitOrientation::Vertical,
        nff::workspace::SplitPlacement::After, 0.5);
    const auto paneC = lowerLeft.pane;
    const auto lowerRight = sourceWorkspace.splitPane(
        paneB, nff::workspace::SplitOrientation::Vertical,
        nff::workspace::SplitPlacement::After, 0.5);
    const auto paneD = lowerRight.pane;
    expect(static_cast<bool>(right) && static_cast<bool>(lowerLeft) &&
               static_cast<bool>(lowerRight),
           "construct aligned 2x2 session workspace");

    expect(sourceWorkspace.normalizeAlignedTwoByTwoRows(),
           "normalize session workspace to independent rows");
    expect(sourceWorkspace.normalizeAlignedTwoByTwoColumns(),
           "pivot session workspace to independent columns");
    const auto leftSplit = sourceWorkspace.root().first()->split();
    const auto rightSplit = sourceWorkspace.root().second()->split();
    expect(sourceWorkspace.setSplitRatio(leftSplit, 0.35),
           "set left column session ratio");
    expect(sourceWorkspace.setSplitRatio(rightSplit, 0.65),
           "set right column session ratio");

    const auto viewA1 = sourceWorkspace.openView(shared.id, paneA);
    const auto viewA2 = sourceWorkspace.openView(shared.id, paneB);
    const auto viewA3 = sourceWorkspace.openView(shared.id, paneC);
    const auto viewA4 = sourceWorkspace.openView(shared.id, paneD);
    const auto alternateView = sourceWorkspace.openView(alternate.id, paneB);
    expect(viewA1 && viewA2 && viewA3 && viewA4 && alternateView,
           "open shared document through independent session views");

    auto* sourceA1 = sourceWorkspace.view(viewA1);
    auto* sourceA4 = sourceWorkspace.view(viewA4);
    expect(sourceA1 != nullptr && sourceA4 != nullptr, "session source views exist");
    if (sourceA1 != nullptr) {
        sourceA1->caretOffset = 2U;
        sourceA1->anchorOffset = 1U;
        sourceA1->firstVisibleLine = 3U;
        sourceA1->wordWrapOverride = false;
    }
    if (sourceA4 != nullptr) {
        sourceA4->caretOffset = 5U;
        sourceA4->anchorOffset = 4U;
        sourceA4->firstVisibleLine = 7U;
        sourceA4->lineNumbersOverride = true;
    }

    expect(sourceWorkspace.setActiveView(paneB, alternateView),
           "set non-first active tab before session save");
    expect(sourceWorkspace.setActiveView(paneD, viewA4),
           "set lower-right active tab before session save");
    expect(sourceWorkspace.setActivePane(paneD),
           "set active pane before session save");

    const auto sessionPath = root / "session.nff";
    const auto captured = nff::session::SessionStore::capture(sourceDocuments, sourceWorkspace);
    error = nff::session::SessionStore::save(sessionPath, captured);
    expect(!error, "save bidirectional workspace session");
    const auto loaded = nff::session::SessionStore::load(sessionPath);
    expect(static_cast<bool>(loaded), "load bidirectional workspace session");

    nff::core::DocumentManager targetDocuments;
    static_cast<void>(targetDocuments.createUntitled());
    nff::workspace::WorkspaceModel targetWorkspace;
    const auto restored = nff::session::SessionRestorer::restore(
        loaded.state, targetDocuments, targetWorkspace);
    expect(restored.workspaceRestored, "restore bidirectional workspace session");
    expect(!restored.usedFallbackWorkspace, "bidirectional session avoids fallback workspace");
    expect(targetWorkspace.paneCount() == 4U && targetWorkspace.viewCount() == 5U,
           "2x2 panes and all tabs survive session restore");

    const auto& restoredRoot = targetWorkspace.root();
    expect(restoredRoot.orientation() == nff::workspace::SplitOrientation::Horizontal &&
               restoredRoot.first() != nullptr && restoredRoot.second() != nullptr &&
               restoredRoot.first()->orientation() == nff::workspace::SplitOrientation::Vertical &&
               restoredRoot.second()->orientation() == nff::workspace::SplitOrientation::Vertical,
           "column-major reciprocal topology survives session round-trip");
    expect(restoredRoot.first() != nullptr && restoredRoot.second() != nullptr &&
               restoredRoot.first()->ratio() == 0.35 && restoredRoot.second()->ratio() == 0.65,
           "independent column ratios survive session round-trip");
    expect(targetWorkspace.activePane() == paneD,
           "active pane survives session round-trip");

    const auto* restoredPaneB = targetWorkspace.pane(paneB);
    const auto* restoredPaneD = targetWorkspace.pane(paneD);
    expect(restoredPaneB != nullptr && restoredPaneB->activeView == alternateView,
           "active tab in non-active pane survives session round-trip");
    expect(restoredPaneD != nullptr && restoredPaneD->activeView == viewA4,
           "active tab in active pane survives session round-trip");

    const auto* sharedRestored = restoredFor(restored, shared.id);
    expect(sharedRestored != nullptr, "shared document runtime mapping exists");
    if (sharedRestored != nullptr) {
        std::size_t sharedViewCount = 0U;
        for (const auto viewId : {viewA1, viewA2, viewA3, viewA4}) {
            const auto* view = targetWorkspace.view(viewId);
            if (view != nullptr && view->document == sharedRestored->runtimeId) {
                ++sharedViewCount;
            }
        }
        expect(sharedViewCount == 4U,
               "multiple independent views still share one remapped runtime document");
    }

    const auto* restoredA1 = targetWorkspace.view(viewA1);
    const auto* restoredA4 = targetWorkspace.view(viewA4);
    expect(restoredA1 != nullptr && restoredA1->caretOffset == 2U &&
               restoredA1->anchorOffset == 1U && restoredA1->firstVisibleLine == 3U &&
               restoredA1->wordWrapOverride == false,
           "first shared view state survives session round-trip");
    expect(restoredA4 != nullptr && restoredA4->caretOffset == 5U &&
               restoredA4->anchorOffset == 4U && restoredA4->firstVisibleLine == 7U &&
               restoredA4->lineNumbersOverride == true,
           "second shared view state remains independent after restore");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testMissingDocumentDoesNotKillWorkspaceRestore() {
    const auto root = freshTempDirectory("nff-session-partial-restore-tests");
    const auto presentPath = root / "present.txt";
    const auto missingPath = root / "missing.txt";
    auto error = nff::storage::FileWriter::writeAtomically(presentPath, bytes("present\n"));
    expect(!error, "write partial restore fixture");
    error = nff::storage::FileWriter::writeAtomically(missingPath, bytes("missing\n"));
    expect(!error, "write temporary missing fixture");

    nff::core::DocumentManager sourceDocuments;
    const auto present = sourceDocuments.open(presentPath);
    const auto missing = sourceDocuments.open(missingPath);
    expect(static_cast<bool>(present) && static_cast<bool>(missing), "open partial restore sources");

    nff::workspace::WorkspaceModel sourceWorkspace;
    static_cast<void>(sourceWorkspace.openView(present.id, sourceWorkspace.primaryPane()));
    const auto split = sourceWorkspace.splitPane(sourceWorkspace.primaryPane(),
                                                 nff::workspace::SplitOrientation::Horizontal);
    static_cast<void>(sourceWorkspace.openView(missing.id, split.pane));
    const auto state = nff::session::SessionStore::capture(sourceDocuments, sourceWorkspace);

    std::filesystem::remove(missingPath, error);
    expect(!error, "remove missing restore fixture");

    nff::core::DocumentManager targetDocuments;
    nff::workspace::WorkspaceModel targetWorkspace;
    const auto restored = nff::session::SessionRestorer::restore(state,
                                                                  targetDocuments,
                                                                  targetWorkspace);
    expect(restored.workspaceRestored, "partial session keeps valid workspace layout");
    expect(restored.restoredDocumentCount() == 1U, "missing document is skipped only");
    expect(targetWorkspace.paneCount() == 2U, "empty pane layout can survive partial restore");
    expect(targetWorkspace.viewCount() == 1U, "view for missing document is pruned");
    expect(!restored.issues.empty(), "missing document is reported as restore issue");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

}

int main() {
    testRecoveryRestoreAndDocumentRemap();
    testBidirectionalWorkspaceRoundTripPreservesTopologyAndViewIdentity();
    testMissingDocumentDoesNotKillWorkspaceRestore();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "all session restore tests passed\n";
    return 0;
}
