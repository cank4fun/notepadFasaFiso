#include "notepadFasaFiso/session/SessionRestorer.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>

namespace nff::session {
namespace {

[[nodiscard]] std::unique_ptr<workspace::WorkspaceSnapshotNode> cloneNode(
    const workspace::WorkspaceSnapshotNode* source) {
    if (source == nullptr) {
        return {};
    }

    auto result = std::make_unique<workspace::WorkspaceSnapshotNode>();
    result->kind = source->kind;
    result->pane = source->pane;
    result->split = source->split;
    result->orientation = source->orientation;
    result->ratio = source->ratio;
    result->first = cloneNode(source->first.get());
    result->second = cloneNode(source->second.get());
    return result;
}

[[nodiscard]] workspace::WorkspaceSnapshot remapWorkspace(
    const workspace::WorkspaceSnapshot& source,
    const std::map<core::DocumentId, core::DocumentId>& documentMap) {
    workspace::WorkspaceSnapshot output;
    output.primaryPane = source.primaryPane;
    output.activePane = source.activePane;
    output.nextPaneId = source.nextPaneId;
    output.nextViewId = source.nextViewId;
    output.nextSplitId = source.nextSplitId;
    output.root = cloneNode(source.root.get());

    std::set<workspace::ViewId> keptViews;
    output.views.reserve(source.views.size());
    for (const auto& view : source.views) {
        const auto mapped = documentMap.find(view.document);
        if (mapped == documentMap.end()) {
            continue;
        }
        auto restored = view;
        restored.document = mapped->second;
        output.views.push_back(restored);
        keptViews.insert(restored.id);
    }

    output.panes.reserve(source.panes.size());
    for (const auto& pane : source.panes) {
        auto restored = pane;
        std::erase_if(restored.views, [&keptViews](const workspace::ViewId view) {
            return !keptViews.contains(view);
        });
        if (restored.activeView && !keptViews.contains(*restored.activeView)) {
            restored.activeView = restored.views.empty()
                                      ? std::optional<workspace::ViewId>{}
                                      : std::optional<workspace::ViewId>{restored.views.back()};
        }
        output.panes.push_back(std::move(restored));
    }
    return output;
}

void restoreFallbackWorkspace(const std::vector<RestoredDocument>& restoredDocuments,
                              workspace::WorkspaceModel& workspace) {
    workspace::WorkspaceModel fresh;
    for (const auto& document : restoredDocuments) {
        static_cast<void>(fresh.openView(document.runtimeId, fresh.primaryPane()));
    }
    static_cast<void>(workspace.restore(fresh.snapshot()));
}

[[nodiscard]] bool regularFileExists(const std::filesystem::path& path) noexcept {
    std::error_code error;
    const auto regular = std::filesystem::is_regular_file(path, error);
    return !error && regular;
}

}

SessionRestoreResult SessionRestorer::restore(const SessionState& state,
                                               core::DocumentManager& documents,
                                               workspace::WorkspaceModel& workspace,
                                               const recovery::RecoveryManager* recovery,
                                               const core::InspectOptions& inspectOptions) {
    SessionRestoreResult result;
    std::map<core::DocumentId, core::DocumentId> documentMap;
    std::set<core::DocumentId> recoveredRuntimeDocuments;

    result.documents.reserve(state.documents.size());
    for (const auto& saved : state.documents) {
        std::optional<recovery::RecoverySnapshot> recovered;
        if (saved.modified && !saved.recoverySnapshot.empty()) {
            if (recovery != nullptr) {
                const auto loaded = recovery->load(saved.recoverySnapshot);
                if (loaded && loaded.snapshot.documentId == saved.id) {
                    recovered = loaded.snapshot;
                } else {
                    result.issues.push_back({SessionRestoreIssueStage::Recovery,
                                             saved.id,
                                             saved.recoverySnapshot,
                                             loaded.error ? loaded.error
                                                          : std::make_error_code(
                                                                std::errc::illegal_byte_sequence)});
                }
            } else {
                result.issues.push_back({SessionRestoreIssueStage::Recovery,
                                         saved.id,
                                         saved.recoverySnapshot,
                                         std::make_error_code(std::errc::operation_not_supported)});
            }
        } else if (saved.modified) {
            result.issues.push_back({SessionRestoreIssueStage::Recovery,
                                     saved.id,
                                     saved.recoverySnapshot,
                                     std::make_error_code(std::errc::no_such_file_or_directory)});
        }

        core::DocumentId runtimeId{};
        RestoredDocumentSource source = RestoredDocumentSource::Disk;
        std::optional<metadata::TextAppearanceMap> recoveredAppearance;

        if (saved.untitled) {
            runtimeId = documents.createUntitled();
            source = RestoredDocumentSource::EmptyUntitled;
        } else if (regularFileExists(saved.path)) {
            const auto opened = documents.openRouted(saved.path, inspectOptions);
            if (opened) {
                runtimeId = opened.id;
            } else if (!recovered) {
                result.issues.push_back({SessionRestoreIssueStage::Document,
                                         saved.id,
                                         saved.path,
                                         opened.error});
                continue;
            }
        } else if (!recovered) {
            result.issues.push_back({SessionRestoreIssueStage::Document,
                                     saved.id,
                                     saved.path,
                                     std::make_error_code(std::errc::no_such_file_or_directory)});
            continue;
        }

        if (!runtimeId) {
            runtimeId = documents.createUntitled();
        }

        if (recovered && !recoveredRuntimeDocuments.contains(runtimeId)) {
            auto* document = documents.get(runtimeId);
            if (document == nullptr) {
                result.issues.push_back({SessionRestoreIssueStage::Document,
                                         saved.id,
                                         saved.path,
                                         std::make_error_code(std::errc::state_not_recoverable)});
                continue;
            }

            const auto recoveryPath = saved.untitled ? std::filesystem::path{} : saved.path;
            const auto error = document->restoreRecovered(recoveryPath,
                                                           std::move(recovered->text),
                                                           recovered->encoding,
                                                           recovered->writeBom);
            if (error) {
                result.issues.push_back({SessionRestoreIssueStage::Recovery,
                                         saved.id,
                                         saved.recoverySnapshot,
                                         error});
            } else {
                source = RestoredDocumentSource::Recovery;
                recoveredAppearance = std::move(recovered->appearance);
                recoveredRuntimeDocuments.insert(runtimeId);
            }
        }

        documentMap.emplace(saved.id, runtimeId);
        result.documents.push_back(
            {saved.id, runtimeId, source, std::move(recoveredAppearance)});
    }

    auto remapped = remapWorkspace(state.workspace, documentMap);
    if (workspace.restore(remapped)) {
        result.workspaceRestored = true;
    } else {
        result.issues.push_back({SessionRestoreIssueStage::Workspace,
                                 {},
                                 {},
                                 std::make_error_code(std::errc::illegal_byte_sequence)});
        restoreFallbackWorkspace(result.documents, workspace);
        result.usedFallbackWorkspace = true;
    }

    return result;
}

}
