#include "notepadFasaFiso/app/ApplicationStateStore.hpp"

#include "notepadFasaFiso/history/Breadcrumbs.hpp"
#include "notepadFasaFiso/platform/Platform.hpp"
#include "notepadFasaFiso/settings/SettingsStore.hpp"

#include <utility>

namespace nff::app {

ApplicationPaths ApplicationPaths::under(std::filesystem::path root) {
    ApplicationPaths result;
    result.root = std::move(root);
    if (result.root.empty()) {
        return result;
    }
    result.settings = result.root / "settings.nff";
    result.session = result.root / "session.nff";
    result.recentFiles = result.root / "recent.nff";
    result.recoveryDirectory = result.root / "recovery";
    result.metadataDirectory = result.root / "metadata";
    return result;
}

ApplicationPaths ApplicationPaths::systemDefault() {
    return under(platform::applicationDataDirectory());
}

ApplicationStateStore::ApplicationStateStore(ApplicationPaths paths)
    : paths_(std::move(paths)) {}

const ApplicationPaths& ApplicationStateStore::paths() const noexcept {
    return paths_;
}

StartupPersistentState ApplicationStateStore::load() const {
    StartupPersistentState state;

    if (regularFileExists(paths_.settings)) {
        auto loaded = settings::SettingsStore::load(paths_.settings);
        if (loaded) {
            state.settings = std::move(loaded.settings);
        } else {
            state.issues.push_back({PersistentStateArea::Settings, paths_.settings, loaded.error});
        }
    }

    if (regularFileExists(paths_.recentFiles)) {
        auto loaded = history::Breadcrumbs::load(paths_.recentFiles);
        if (loaded) {
            state.recentFiles = std::move(loaded.recent);
        } else {
            state.issues.push_back({PersistentStateArea::RecentFiles,
                                    paths_.recentFiles, loaded.error});
        }
    }

    if (state.settings.restorePreviousSession && regularFileExists(paths_.session)) {
        auto loaded = session::SessionStore::load(paths_.session);
        if (loaded) {
            state.session = std::move(loaded.state);
        } else {
            state.issues.push_back({PersistentStateArea::Session, paths_.session, loaded.error});
        }
    }

    std::error_code recoveryError;
    if (!paths_.recoveryDirectory.empty()) {
        recovery::RecoveryManager recovery(paths_.recoveryDirectory);
        state.recoverySnapshots = recovery.list(recoveryError);
    }
    if (recoveryError) {
        state.recoverySnapshots.clear();
        state.issues.push_back({PersistentStateArea::Recovery,
                                paths_.recoveryDirectory, recoveryError});
    }

    return state;
}

PersistentSaveResult ApplicationStateStore::save(
    const settings::AppSettings& settings,
    const history::RecentFiles& recentFiles,
    const core::DocumentManager& documents,
    const workspace::WorkspaceModel& workspace,
    const recovery::RecoveryManager* recovery) const {
    PersistentSaveResult result;

    if (!paths_.root.empty()) {
        if (const auto directoryError = platform::ensurePrivateDirectory(paths_.root)) {
            result.issues.push_back(
                {PersistentStateArea::Settings, paths_.root, directoryError});
            return result;
        }
    }

    const auto savePrivate = [&result](const PersistentStateArea area,
                                       const std::filesystem::path& path,
                                       std::error_code error) {
        if (!error) {
            error = platform::hardenPrivateFile(path);
        }
        if (error) {
            result.issues.push_back({area, path, error});
        }
    };

    savePrivate(PersistentStateArea::Settings,
                paths_.settings,
                settings::SettingsStore::save(paths_.settings, settings));
    savePrivate(PersistentStateArea::RecentFiles,
                paths_.recentFiles,
                history::Breadcrumbs::save(paths_.recentFiles, recentFiles));

    const auto captured = session::SessionStore::capture(documents, workspace, recovery);
    savePrivate(PersistentStateArea::Session,
                paths_.session,
                session::SessionStore::save(paths_.session, captured));

    return result;
}

bool ApplicationStateStore::regularFileExists(const std::filesystem::path& path) noexcept {
    if (path.empty()) {
        return false;
    }
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

}
