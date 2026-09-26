#pragma once

#include "notepadFasaFiso/app/ApplicationStateStore.hpp"
#include "notepadFasaFiso/app/DocumentFileMonitor.hpp"
#include "notepadFasaFiso/recovery/AutoSaveManager.hpp"
#include "notepadFasaFiso/recovery/RecoveryManager.hpp"
#include "notepadFasaFiso/session/SessionRestorer.hpp"

#include <chrono>
#include <filesystem>
#include <system_error>
#include <vector>

namespace nff::app {

struct ApplicationLifecycleStartup final {
    bool sessionAttempted{false};
    session::SessionRestoreResult session{};
    std::vector<PersistentStateIssue> persistentIssues;
    std::vector<std::filesystem::path> unclaimedRecoverySnapshots;
    std::error_code sessionSaveError{};
};

struct ApplicationMaintenanceResult final {
    std::vector<recovery::AutoSaveResult> autoSave;
    std::vector<DocumentFileEvent> fileEvents;
    std::error_code sessionSaveError{};

    [[nodiscard]] bool changed() const noexcept {
        return !autoSave.empty() || !fileEvents.empty();
    }
};

class ApplicationLifecycle final {
public:
    using Clock = recovery::AutoSaveManager::Clock;

    ApplicationLifecycle(ApplicationStateStore stateStore,
                         StartupPersistentState startupState,
                         core::DocumentManager& documents,
                         workspace::WorkspaceModel& workspace);

    [[nodiscard]] settings::AppSettings& settings() noexcept;
    [[nodiscard]] const settings::AppSettings& settings() const noexcept;
    [[nodiscard]] history::RecentFiles& recentFiles() noexcept;
    [[nodiscard]] const history::RecentFiles& recentFiles() const noexcept;
    [[nodiscard]] recovery::RecoveryManager& recovery() noexcept;
    [[nodiscard]] const recovery::RecoveryManager& recovery() const noexcept;
    [[nodiscard]] const ApplicationPaths& paths() const noexcept;

    [[nodiscard]] ApplicationLifecycleStartup start();

    void noteEdited(core::DocumentId documentId,
                    Clock::time_point now = Clock::now());
    [[nodiscard]] std::error_code noteOpened(const std::filesystem::path& path,
                                             core::OpenMode mode,
                                             std::uint64_t unixMilliseconds = 0);
    [[nodiscard]] recovery::AutoSaveResult focusLost(
        core::DocumentId documentId,
        Clock::time_point now = Clock::now());
    [[nodiscard]] std::error_code forget(core::DocumentId documentId,
                                         bool discardRecovery = true) noexcept;

    [[nodiscard]] ApplicationMaintenanceResult poll(
        Clock::time_point now = Clock::now());
    [[nodiscard]] std::error_code saveSettings() const;
    [[nodiscard]] std::error_code saveRecentFiles() const;
    [[nodiscard]] std::error_code saveSession() const;
    [[nodiscard]] PersistentSaveResult saveAll();

private:
    [[nodiscard]] bool outcomeNeedsSessionSave(recovery::AutoSaveOutcome outcome) const noexcept;
    [[nodiscard]] std::vector<std::filesystem::path> unclaimedRecoverySnapshots() const;
    [[nodiscard]] bool checkpointRecoveredDocuments(session::SessionRestoreResult& restored);

    ApplicationStateStore stateStore_;
    core::DocumentManager* documents_{};
    workspace::WorkspaceModel* workspace_{};
    settings::AppSettings settings_{};
    history::RecentFiles recentFiles_{};
    recovery::RecoveryManager recovery_;
    recovery::AutoSaveManager autoSave_;
    DocumentFileMonitor fileMonitor_{};
    std::optional<session::SessionState> startupSession_{};
    std::vector<std::filesystem::path> startupRecoverySnapshots_;
    std::vector<PersistentStateIssue> startupIssues_;
    bool started_{false};
};

}
