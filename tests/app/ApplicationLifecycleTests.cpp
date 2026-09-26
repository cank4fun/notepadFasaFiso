#include "notepadFasaFiso/app/ApplicationLifecycle.hpp"
#include "notepadFasaFiso/history/Breadcrumbs.hpp"
#include "notepadFasaFiso/session/SessionStore.hpp"
#include "notepadFasaFiso/settings/SettingsStore.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::filesystem::path tempRoot(const std::string_view name) {
    return std::filesystem::temp_directory_path() /
           ("nff-lifecycle-" + std::string(name));
}

void write(const std::filesystem::path& path, const std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void testStartupRestoresRecoveredSessionAndRehomesCheckpoint() {
    const auto root = tempRoot("restore");
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    const auto paths = nff::app::ApplicationPaths::under(root / "state");
    nff::core::DocumentManager sourceDocuments;
    nff::workspace::WorkspaceModel sourceWorkspace;
    const auto documentId = sourceDocuments.createUntitled();
    auto* sourceDocument = sourceDocuments.get(documentId);
    sourceDocument->replaceText("recovered text\n");
    const auto view = sourceWorkspace.openView(documentId, sourceWorkspace.primaryPane());
    expect(static_cast<bool>(view), "source session view opens");

    nff::recovery::RecoveryManager oldRecovery(paths.recoveryDirectory, 111U);
    expect(!oldRecovery.checkpoint(documentId, *sourceDocument), "source recovery checkpoint saves");
    const auto oldSnapshot = oldRecovery.snapshotPath(documentId);
    const auto state = nff::session::SessionStore::capture(
        sourceDocuments, sourceWorkspace, &oldRecovery);
    expect(!nff::session::SessionStore::save(paths.session, state), "source session saves");

    nff::settings::AppSettings settings;
    expect(!nff::settings::SettingsStore::save(paths.settings, settings), "settings save");

    nff::app::ApplicationStateStore store(paths);
    auto startup = store.load();
    nff::core::DocumentManager documents;
    nff::workspace::WorkspaceModel workspace;
    nff::app::ApplicationLifecycle lifecycle(
        nff::app::ApplicationStateStore(paths), std::move(startup), documents, workspace);

    const auto started = lifecycle.start();
    expect(started.sessionAttempted, "startup attempts persisted session restore");
    expect(started.session.restoredDocumentCount() == 1U, "one recovered document restored");
    expect(workspace.viewCount() == 1U, "restored workspace contains its view");
    expect(documents.ids().size() == 1U, "restored document manager contains one document");
    if (!documents.ids().empty()) {
        const auto restoredId = documents.ids().front();
        const auto* restored = documents.get(restoredId);
        expect(restored != nullptr && restored->modified(), "recovered document stays modified");
        expect(restored != nullptr && restored->text() == "recovered text\n",
               "recovered document text survives restore");
        expect(std::filesystem::is_regular_file(lifecycle.recovery().snapshotPath(restoredId)),
               "recovered document receives current-session checkpoint");
    }
    expect(!std::filesystem::exists(oldSnapshot),
           "successfully rehomed recovery does not leave a stale orphan snapshot");

    const auto persisted = nff::session::SessionStore::load(paths.session);
    expect(static_cast<bool>(persisted), "rehomed restored session persists");
    expect(persisted && !persisted.state.documents.empty() &&
               !persisted.state.documents.front().recoverySnapshot.empty(),
           "rehomed session points at a recovery snapshot");

    std::filesystem::remove_all(root, error);
}

void testStartupReportsUnclaimedRecoveryWithoutDeletingIt() {
    const auto root = tempRoot("unclaimed-recovery");
    std::error_code error;
    std::filesystem::remove_all(root, error);

    const auto paths = nff::app::ApplicationPaths::under(root / "state");
    nff::core::DocumentManager sourceDocuments;
    const auto documentId = sourceDocuments.createUntitled();
    auto* document = sourceDocuments.get(documentId);
    document->replaceText("orphan recovery text\n");
    nff::recovery::RecoveryManager oldRecovery(paths.recoveryDirectory, 222U);
    expect(!oldRecovery.checkpoint(documentId, *document),
           "unclaimed recovery fixture saves");
    const auto orphanPath = oldRecovery.snapshotPath(documentId);

    nff::app::ApplicationStateStore store(paths);
    auto startup = store.load();
    nff::core::DocumentManager documents;
    nff::workspace::WorkspaceModel workspace;
    nff::app::ApplicationLifecycle lifecycle(
        nff::app::ApplicationStateStore(paths), std::move(startup), documents, workspace);

    const auto started = lifecycle.start();
    expect(started.unclaimedRecoverySnapshots.size() == 1U,
           "startup reports unclaimed recovery snapshot");
    expect(!started.unclaimedRecoverySnapshots.empty() &&
               started.unclaimedRecoverySnapshots.front() == orphanPath,
           "startup returns the actual orphan snapshot path");
    expect(std::filesystem::is_regular_file(orphanPath),
           "startup never silently deletes unclaimed recovery");

    std::filesystem::remove_all(root, error);
}

void testAutosaveCheckpointUpdatesSessionReference() {
    const auto root = tempRoot("autosave");
    std::error_code error;
    std::filesystem::remove_all(root, error);

    const auto paths = nff::app::ApplicationPaths::under(root / "state");
    nff::core::DocumentManager documents;
    nff::workspace::WorkspaceModel workspace;
    nff::app::StartupPersistentState startup;
    startup.settings.autoSave.recoveryDelay = std::chrono::milliseconds{100};
    nff::app::ApplicationLifecycle lifecycle(
        nff::app::ApplicationStateStore(paths), std::move(startup), documents, workspace);

    const auto documentId = documents.createUntitled();
    const auto view = workspace.openView(documentId, workspace.primaryPane());
    expect(static_cast<bool>(view), "autosave lifecycle view opens");
    auto* document = documents.get(documentId);
    document->replaceText("changed\n");

    const auto start = nff::app::ApplicationLifecycle::Clock::time_point{};
    lifecycle.noteEdited(documentId, start);
    const auto maintenance = lifecycle.poll(start + std::chrono::milliseconds{150});
    expect(maintenance.autoSave.size() == 1U, "autosave poll creates one recovery result");
    expect(!maintenance.sessionSaveError, "autosave checkpoint also saves session state");
    if (!maintenance.autoSave.empty()) {
        expect(maintenance.autoSave.front().outcome ==
                   nff::recovery::AutoSaveOutcome::RecoveryCheckpoint,
               "autosave result is a recovery checkpoint");
    }

    const auto persisted = nff::session::SessionStore::load(paths.session);
    expect(static_cast<bool>(persisted), "autosave session can be loaded");
    expect(persisted && persisted.state.documents.size() == 1U &&
               !persisted.state.documents.front().recoverySnapshot.empty(),
           "autosave session references checkpoint for crash recovery");

    std::filesystem::remove_all(root, error);
}

void testRuntimeAutosavePolicyTracksMutableSettings() {
    const auto root = tempRoot("runtime-settings");
    std::error_code error;
    std::filesystem::remove_all(root, error);

    const auto paths = nff::app::ApplicationPaths::under(root / "state");
    nff::core::DocumentManager documents;
    nff::workspace::WorkspaceModel workspace;
    nff::app::ApplicationLifecycle lifecycle(
        nff::app::ApplicationStateStore(paths), {}, documents, workspace);

    lifecycle.settings().autoSave.recoveryDelay = std::chrono::milliseconds{100};
    const auto documentId = documents.createUntitled();
    auto* document = documents.get(documentId);
    document->replaceText("runtime policy\n");

    const auto start = nff::app::ApplicationLifecycle::Clock::time_point{};
    lifecycle.noteEdited(documentId, start);
    const auto maintenance = lifecycle.poll(start + std::chrono::milliseconds{150});
    expect(maintenance.autoSave.size() == 1U,
           "runtime autosave settings take effect without restarting");
    if (!maintenance.autoSave.empty()) {
        expect(maintenance.autoSave.front().outcome ==
                   nff::recovery::AutoSaveOutcome::RecoveryCheckpoint,
               "runtime autosave delay produces a recovery checkpoint");
    }

    std::filesystem::remove_all(root, error);
}

void testSaveAllDoesNotReplaceSessionWhenRecoveryFails() {
    const auto root = tempRoot("save-failure");
    std::error_code error;
    std::filesystem::remove_all(root, error);

    const auto paths = nff::app::ApplicationPaths::under(root / "state");
    std::filesystem::create_directories(paths.root, error);
    write(paths.recoveryDirectory, "blocks recovery directory");
    write(paths.session, "existing-session");

    nff::core::DocumentManager documents;
    nff::workspace::WorkspaceModel workspace;
    nff::app::ApplicationLifecycle lifecycle(
        nff::app::ApplicationStateStore(paths), {}, documents, workspace);
    const auto documentId = documents.createUntitled();
    expect(static_cast<bool>(workspace.openView(documentId, workspace.primaryPane())),
           "save failure workspace view opens");
    auto* document = documents.get(documentId);
    document->replaceText("unsaved payload\n");

    const auto saved = lifecycle.saveAll();
    expect(!saved, "saveAll reports recovery failure");
    expect(!saved.issues.empty() &&
               saved.issues.front().area == nff::app::PersistentStateArea::Recovery,
           "recovery failure is reported as recovery state issue");

    std::ifstream session(paths.session, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(session)),
                               std::istreambuf_iterator<char>());
    expect(contents == "existing-session",
           "failed recovery checkpoint never replaces the last session file");

    std::filesystem::remove_all(root, error);
}

void testRecentFilesAndWatcherFlowThroughLifecycle() {
    const auto root = tempRoot("recent-watch");
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const auto path = root / "note.txt";
    write(path, "one\n");

    const auto paths = nff::app::ApplicationPaths::under(root / "state");
    nff::core::DocumentManager documents;
    nff::workspace::WorkspaceModel workspace;
    nff::app::ApplicationLifecycle lifecycle(
        nff::app::ApplicationStateStore(paths), {}, documents, workspace);

    const auto opened = documents.open(path);
    expect(static_cast<bool>(opened), "watched lifecycle document opens");
    const auto view = workspace.openView(opened.id, workspace.primaryPane());
    expect(static_cast<bool>(view), "watched lifecycle view opens");
    static_cast<void>(lifecycle.start());
    expect(!lifecycle.noteOpened(path, nff::core::OpenMode::Editor, 1234U),
           "recent file touch persists");

    const auto recent = nff::history::Breadcrumbs::load(paths.recentFiles);
    expect(static_cast<bool>(recent) && recent.recent.size() == 1U,
           "recent file store updated by lifecycle");

    write(path, "two and longer\n");
    const auto maintenance = lifecycle.poll();
    expect(maintenance.fileEvents.size() == 1U,
           "lifecycle reports external file watcher event");
    if (!maintenance.fileEvents.empty()) {
        expect(maintenance.fileEvents.front().documentId == opened.id,
               "external file event retains document id");
    }

    std::filesystem::remove_all(root, error);
}

}

int main() {
    testStartupRestoresRecoveredSessionAndRehomesCheckpoint();
    testStartupReportsUnclaimedRecoveryWithoutDeletingIt();
    testAutosaveCheckpointUpdatesSessionReference();
    testRuntimeAutosavePolicyTracksMutableSettings();
    testRecentFilesAndWatcherFlowThroughLifecycle();
    testSaveAllDoesNotReplaceSessionWhenRecoveryFails();
    return failures == 0 ? 0 : 1;
}
