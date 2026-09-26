#include "notepadFasaFiso/recovery/AutoSaveManager.hpp"

#include "notepadFasaFiso/storage/FileState.hpp"

namespace nff::recovery {

AutoSaveManager::AutoSaveManager(core::DocumentManager& documents,
                                 RecoveryManager& recovery,
                                 AutoSavePolicy policy)
    : documents_(documents), recovery_(recovery), policy_(policy) {}

void AutoSaveManager::setPolicy(AutoSavePolicy policy) noexcept {
    policy_ = policy;
}

const AutoSavePolicy& AutoSaveManager::policy() const noexcept {
    return policy_;
}

void AutoSaveManager::noteEdited(const core::DocumentId documentId, const Clock::time_point now) {
    const auto* document = documents_.get(documentId);
    if (document == nullptr) {
        return;
    }

    auto& tracked = tracked_[documentId];
    tracked.lastEdit = now;
    tracked.observedRevision = document->revision();
}

std::vector<AutoSaveResult> AutoSaveManager::poll(const Clock::time_point now) {
    std::vector<AutoSaveResult> results;
    for (auto iterator = tracked_.begin(); iterator != tracked_.end();) {
        if (documents_.get(iterator->first) == nullptr) {
            iterator = tracked_.erase(iterator);
            continue;
        }

        auto result = process(iterator->first, iterator->second, now, false);
        if (result.outcome != AutoSaveOutcome::None) {
            results.push_back(result);
        }
        ++iterator;
    }
    return results;
}

AutoSaveResult AutoSaveManager::focusLost(const core::DocumentId documentId,
                                          const Clock::time_point now) {
    auto* document = documents_.get(documentId);
    if (document == nullptr) {
        return {documentId, AutoSaveOutcome::Failed,
                std::make_error_code(std::errc::invalid_argument)};
    }

    auto& tracked = tracked_[documentId];
    if (tracked.observedRevision != document->revision()) {
        tracked.observedRevision = document->revision();
        tracked.lastEdit = now;
    }
    return process(documentId, tracked, now, true);
}

void AutoSaveManager::forget(const core::DocumentId documentId) noexcept {
    tracked_.erase(documentId);
}

AutoSaveResult AutoSaveManager::process(const core::DocumentId documentId,
                                        TrackedDocument& tracked,
                                        const Clock::time_point now,
                                        const bool focusLoss) {
    auto* document = documents_.get(documentId);
    if (document == nullptr) {
        return {documentId, AutoSaveOutcome::Failed,
                std::make_error_code(std::errc::invalid_argument)};
    }

    if (!document->modified()) {
        if (tracked.recoveryRevision != 0) {
            const auto error = recovery_.discard(documentId);
            if (error) {
                return {documentId, AutoSaveOutcome::Failed, error};
            }
            tracked.recoveryRevision = 0;
        }
        tracked.savedRevision = document->revision();
        return {documentId, AutoSaveOutcome::None, {}};
    }

    if (tracked.observedRevision != document->revision()) {
        tracked.observedRevision = document->revision();
        tracked.lastEdit = now;
        return {documentId, AutoSaveOutcome::None, {}};
    }

    const auto inactiveFor = now - tracked.lastEdit;
    const bool shouldSaveForInactivity =
        !focusLoss && policy_.saveRealFiles && !document->path().empty() &&
        inactiveFor >= policy_.saveDelay && tracked.savedRevision != document->revision();
    const bool shouldSaveForFocusLoss = focusLoss && policy_.saveRealFiles &&
                                        policy_.saveOnFocusLoss && !document->path().empty() &&
                                        tracked.savedRevision != document->revision();

    if (shouldSaveForInactivity || shouldSaveForFocusLoss) {
        const auto externalState = document->externalChangeState();
        if (externalState == storage::FileChangeState::Modified ||
            externalState == storage::FileChangeState::Deleted) {
            const auto recoveryResult = checkpoint(documentId, *document, tracked);
            if (!recoveryResult) {
                return recoveryResult;
            }
            return {documentId, AutoSaveOutcome::ExternalConflict, {}};
        }
        if (externalState == storage::FileChangeState::Inaccessible) {
            const auto recoveryResult = checkpoint(documentId, *document, tracked);
            if (!recoveryResult) {
                return recoveryResult;
            }
            return {documentId, AutoSaveOutcome::Failed,
                    std::make_error_code(std::errc::io_error)};
        }

        const auto error = document->save();
        if (error) {
            const auto recoveryResult = checkpoint(documentId, *document, tracked);
            if (!recoveryResult) {
                return recoveryResult;
            }
            return {documentId, AutoSaveOutcome::Failed, error};
        }

        tracked.savedRevision = document->revision();
        tracked.recoveryRevision = 0;
        const auto discardError = recovery_.discard(documentId);
        if (discardError) {
            return {documentId, AutoSaveOutcome::Failed, discardError};
        }
        return {documentId, AutoSaveOutcome::FileSaved, {}};
    }

    const bool recoveryDue = policy_.recoveryEnabled &&
                             tracked.recoveryRevision != document->revision() &&
                             (focusLoss || inactiveFor >= policy_.recoveryDelay);
    if (recoveryDue) {
        return checkpoint(documentId, *document, tracked);
    }

    return {documentId, AutoSaveOutcome::None, {}};
}

AutoSaveResult AutoSaveManager::checkpoint(const core::DocumentId documentId,
                                           core::Document& document,
                                           TrackedDocument& tracked) {
    if (!policy_.recoveryEnabled) {
        return {documentId, AutoSaveOutcome::None, {}};
    }

    const auto error = recovery_.checkpoint(documentId, document);
    if (error) {
        return {documentId, AutoSaveOutcome::Failed, error};
    }
    tracked.recoveryRevision = document.revision();
    return {documentId, AutoSaveOutcome::RecoveryCheckpoint, {}};
}

}
