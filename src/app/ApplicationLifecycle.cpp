#include "notepadFasaFiso/app/ApplicationLifecycle.hpp"

#include "notepadFasaFiso/history/Breadcrumbs.hpp"
#include "notepadFasaFiso/session/SessionStore.hpp"
#include "notepadFasaFiso/settings/SettingsStore.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace nff::app {
namespace {

[[nodiscard]] std::filesystem::path normalizedPath(const std::filesystem::path& path) {
    if (path.empty()) {
        return {};
    }
    std::error_code error;
    auto normalized = std::filesystem::weakly_canonical(path, error);
    if (!error) {
        return normalized.lexically_normal();
    }
    return path.lexically_normal();
}

}

ApplicationLifecycle::ApplicationLifecycle(ApplicationStateStore stateStore,
                                           StartupPersistentState startupState,
                                           core::DocumentManager& documents,
                                           workspace::WorkspaceModel& workspace)
    : stateStore_(std::move(stateStore)),
      documents_(&documents),
      workspace_(&workspace),
      settings_(std::move(startupState.settings)),
      recentFiles_(std::move(startupState.recentFiles)),
      recovery_(stateStore_.paths().recoveryDirectory),
      autoSave_(documents, recovery_, settings_.autoSave),
      startupSession_(std::move(startupState.session)),
      startupRecoverySnapshots_(std::move(startupState.recoverySnapshots)),
      startupIssues_(std::move(startupState.issues)) {}

settings::AppSettings& ApplicationLifecycle::settings() noexcept {
    return settings_;
}

const settings::AppSettings& ApplicationLifecycle::settings() const noexcept {
    return settings_;
}

history::RecentFiles& ApplicationLifecycle::recentFiles() noexcept {
    return recentFiles_;
}

const history::RecentFiles& ApplicationLifecycle::recentFiles() const noexcept {
    return recentFiles_;
}

recovery::RecoveryManager& ApplicationLifecycle::recovery() noexcept {
    return recovery_;
}

const recovery::RecoveryManager& ApplicationLifecycle::recovery() const noexcept {
    return recovery_;
}

const ApplicationPaths& ApplicationLifecycle::paths() const noexcept {
    return stateStore_.paths();
}

ApplicationLifecycleStartup ApplicationLifecycle::start() {
    ApplicationLifecycleStartup result;
    if (started_) {
        return result;
    }
    started_ = true;
    result.persistentIssues = startupIssues_;
    result.unclaimedRecoverySnapshots = unclaimedRecoverySnapshots();

    if (settings_.restorePreviousSession && startupSession_) {
        result.sessionAttempted = true;
        result.session = session::SessionRestorer::restore(
            *startupSession_, *documents_, *workspace_, &recovery_, settings_.inspectOptions);
        const bool recoveredCheckpointed = checkpointRecoveredDocuments(result.session);
        if (result.session.restoredDocumentCount() != 0U && recoveredCheckpointed) {
            result.sessionSaveError = saveSession();
        }
    }

    fileMonitor_.sync(*documents_);
    startupSession_.reset();
    startupRecoverySnapshots_.clear();
    startupIssues_.clear();
    return result;
}

void ApplicationLifecycle::noteEdited(const core::DocumentId documentId,
                                      const Clock::time_point now) {
    autoSave_.noteEdited(documentId, now);
}

std::error_code ApplicationLifecycle::noteOpened(const std::filesystem::path& path,
                                                 const core::OpenMode mode,
                                                 const std::uint64_t unixMilliseconds) {
    if (path.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    recentFiles_.touch(path, mode, unixMilliseconds);
    fileMonitor_.sync(*documents_);
    return saveRecentFiles();
}

recovery::AutoSaveResult ApplicationLifecycle::focusLost(
    const core::DocumentId documentId,
    const Clock::time_point now) {
    autoSave_.setPolicy(settings_.autoSave);
    auto result = autoSave_.focusLost(documentId, now);
    if (outcomeNeedsSessionSave(result.outcome)) {
        const auto sessionError = saveSession();
        if (sessionError && !result.error) {
            result.error = sessionError;
            result.outcome = recovery::AutoSaveOutcome::Failed;
        }
    }
    return result;
}

std::error_code ApplicationLifecycle::forget(const core::DocumentId documentId,
                                             const bool discardRecovery) noexcept {
    autoSave_.forget(documentId);
    if (!discardRecovery) {
        return {};
    }
    return recovery_.discard(documentId);
}

ApplicationMaintenanceResult ApplicationLifecycle::poll(const Clock::time_point now) {
    autoSave_.setPolicy(settings_.autoSave);
    ApplicationMaintenanceResult result;
    result.autoSave = autoSave_.poll(now);

    bool saveSessionNow = false;
    for (const auto& item : result.autoSave) {
        saveSessionNow = outcomeNeedsSessionSave(item.outcome) || saveSessionNow;
    }
    if (saveSessionNow) {
        result.sessionSaveError = saveSession();
    }

    fileMonitor_.sync(*documents_);
    result.fileEvents = fileMonitor_.poll();
    return result;
}

std::error_code ApplicationLifecycle::saveSettings() const {
    return settings::SettingsStore::save(stateStore_.paths().settings, settings_);
}

std::error_code ApplicationLifecycle::saveRecentFiles() const {
    return history::Breadcrumbs::save(stateStore_.paths().recentFiles, recentFiles_);
}

std::error_code ApplicationLifecycle::saveSession() const {
    const auto state = session::SessionStore::capture(*documents_, *workspace_, &recovery_);
    return session::SessionStore::save(stateStore_.paths().session, state);
}

PersistentSaveResult ApplicationLifecycle::saveAll() {
    PersistentSaveResult result;
    bool recoveryReady = true;
    for (const auto id : documents_->ids()) {
        const auto* document = documents_->get(id);
        if (document == nullptr || !document->modified()) {
            continue;
        }
        if (const auto error = recovery_.checkpoint(id, *document)) {
            recoveryReady = false;
            result.issues.push_back({PersistentStateArea::Recovery,
                                     recovery_.snapshotPath(id),
                                     error});
        }
    }

    if (!recoveryReady) {
        if (const auto error = saveSettings()) {
            result.issues.push_back({PersistentStateArea::Settings,
                                     stateStore_.paths().settings,
                                     error});
        }
        if (const auto error = saveRecentFiles()) {
            result.issues.push_back({PersistentStateArea::RecentFiles,
                                     stateStore_.paths().recentFiles,
                                     error});
        }
        return result;
    }

    auto saved = stateStore_.save(settings_, recentFiles_, *documents_, *workspace_, &recovery_);
    result.issues.insert(result.issues.end(), saved.issues.begin(), saved.issues.end());
    return result;
}

bool ApplicationLifecycle::outcomeNeedsSessionSave(
    const recovery::AutoSaveOutcome outcome) const noexcept {
    switch (outcome) {
    case recovery::AutoSaveOutcome::RecoveryCheckpoint:
    case recovery::AutoSaveOutcome::FileSaved:
    case recovery::AutoSaveOutcome::ExternalConflict:
        return true;
    case recovery::AutoSaveOutcome::None:
    case recovery::AutoSaveOutcome::Failed:
        return false;
    }
    return false;
}

std::vector<std::filesystem::path> ApplicationLifecycle::unclaimedRecoverySnapshots() const {
    std::set<std::filesystem::path> referenced;
    if (startupSession_) {
        for (const auto& document : startupSession_->documents) {
            if (!document.recoverySnapshot.empty()) {
                referenced.insert(normalizedPath(document.recoverySnapshot));
            }
        }
    }

    std::vector<std::filesystem::path> result;
    result.reserve(startupRecoverySnapshots_.size());
    for (const auto& snapshot : startupRecoverySnapshots_) {
        if (!referenced.contains(normalizedPath(snapshot))) {
            result.push_back(snapshot);
        }
    }
    return result;
}

bool ApplicationLifecycle::checkpointRecoveredDocuments(
    session::SessionRestoreResult& restored) {
    const auto now = Clock::now();
    bool success = true;
    for (const auto& item : restored.documents) {
        if (item.source != session::RestoredDocumentSource::Recovery) {
            continue;
        }
        const auto* document = documents_->get(item.runtimeId);
        if (document == nullptr || !document->modified()) {
            continue;
        }
        const auto* recoveredAppearance = item.recoveredAppearance.has_value()
                                              ? &*item.recoveredAppearance
                                              : nullptr;
        const auto error = recovery_.checkpoint(
            item.runtimeId, *document, recoveredAppearance);
        if (error) {
            success = false;
            restored.issues.push_back({session::SessionRestoreIssueStage::Recovery,
                                       item.persistedId,
                                       recovery_.snapshotPath(item.runtimeId),
                                       error});
            continue;
        }

        if (startupSession_) {
            const auto saved = std::find_if(
                startupSession_->documents.begin(), startupSession_->documents.end(),
                [&item](const auto& candidate) { return candidate.id == item.persistedId; });
            if (saved != startupSession_->documents.end() &&
                !saved->recoverySnapshot.empty() &&
                normalizedPath(saved->recoverySnapshot) !=
                    normalizedPath(recovery_.snapshotPath(item.runtimeId))) {
                const auto discardError = recovery_.discardSnapshot(saved->recoverySnapshot);
                if (discardError) {
                    restored.issues.push_back({session::SessionRestoreIssueStage::Recovery,
                                               item.persistedId,
                                               saved->recoverySnapshot,
                                               discardError});
                }
            }
        }
        autoSave_.noteEdited(item.runtimeId, now);
    }
    return success;
}

}
