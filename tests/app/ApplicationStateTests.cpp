#include "notepadFasaFiso/app/ApplicationStateStore.hpp"
#include "notepadFasaFiso/app/DocumentFileMonitor.hpp"

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
           ("nff-appstate-" + std::string(name));
}

void write(const std::filesystem::path& path, const std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void testPersistentStateRoundtrip() {
    const auto root = tempRoot("roundtrip");
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    const auto documentPath = root / "note.txt";
    write(documentPath, "hello\n");

    nff::core::DocumentManager documents;
    const auto opened = documents.open(documentPath);
    expect(static_cast<bool>(opened), "document opens for app state test");

    nff::workspace::WorkspaceModel workspace;
    const auto view = workspace.openView(opened.id, workspace.primaryPane());
    expect(static_cast<bool>(view), "workspace view opens");

    nff::settings::AppSettings settings;
    settings.sidebarVisible = false;
    nff::history::RecentFiles recent;
    recent.touch(documentPath, nff::core::OpenMode::Editor, 1234U);
    nff::recovery::RecoveryManager recovery(root / "recovery", 77U);

    nff::app::ApplicationStateStore store(nff::app::ApplicationPaths::under(root / "state"));
    const auto saved = store.save(settings, recent, documents, workspace, &recovery);
    expect(static_cast<bool>(saved), "application state saves without issues");

    const auto loaded = store.load();
    expect(loaded.issues.empty(), "application state loads without issues");
    expect(!loaded.settings.sidebarVisible, "settings restored");
    expect(loaded.recentFiles.size() == 1U, "recent files restored");
    expect(loaded.session.has_value(), "session restored");
    if (loaded.session) {
        expect(loaded.session->documents.size() == 1U, "session document count restored");
        expect(loaded.session->workspace.views.size() == 1U, "session view count restored");
    }

    std::filesystem::remove_all(root, error);
}

void testDocumentMonitorSync() {
    const auto root = tempRoot("monitor");
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const auto path = root / "watched.txt";
    write(path, "one\n");

    nff::core::DocumentManager documents;
    const auto opened = documents.open(path);
    expect(static_cast<bool>(opened), "monitor test document opens");

    nff::app::DocumentFileMonitor monitor;
    monitor.sync(documents);
    expect(monitor.size() == 1U, "document monitor tracks open file");

    write(path, "longer content\n");
    const auto events = monitor.poll();
    expect(events.size() == 1U, "document monitor forwards file event");
    if (!events.empty()) {
        expect(events[0].documentId == opened.id, "file event maps to document id");
    }

    expect(documents.close(opened.id), "monitor document closes");
    monitor.sync(documents);
    expect(monitor.size() == 0U, "monitor drops closed document");

    std::filesystem::remove_all(root, error);
}

}

int main() {
    testPersistentStateRoundtrip();
    testDocumentMonitorSync();
    return failures == 0 ? 0 : 1;
}
