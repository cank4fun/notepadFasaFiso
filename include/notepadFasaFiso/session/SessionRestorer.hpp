#pragma once

#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/recovery/RecoveryManager.hpp"
#include "notepadFasaFiso/session/SessionStore.hpp"
#include "notepadFasaFiso/workspace/WorkspaceModel.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <system_error>
#include <vector>

namespace nff::session {

enum class RestoredDocumentSource : std::uint8_t {
    Disk,
    Recovery,
    EmptyUntitled,
};

struct RestoredDocument final {
    core::DocumentId persistedId{};
    core::DocumentId runtimeId{};
    RestoredDocumentSource source{RestoredDocumentSource::Disk};
    std::optional<metadata::TextAppearanceMap> recoveredAppearance;
};

enum class SessionRestoreIssueStage : std::uint8_t {
    Recovery,
    Document,
    Workspace,
};

struct SessionRestoreIssue final {
    SessionRestoreIssueStage stage{SessionRestoreIssueStage::Document};
    core::DocumentId persistedId{};
    std::filesystem::path path;
    std::error_code error;
};

struct SessionRestoreResult final {
    std::vector<RestoredDocument> documents;
    std::vector<SessionRestoreIssue> issues;
    bool workspaceRestored{false};
    bool usedFallbackWorkspace{false};

    [[nodiscard]] std::size_t restoredDocumentCount() const noexcept {
        return documents.size();
    }
};

class SessionRestorer final {
public:
    [[nodiscard]] static SessionRestoreResult restore(
        const SessionState& state,
        core::DocumentManager& documents,
        workspace::WorkspaceModel& workspace,
        const recovery::RecoveryManager* recovery = nullptr,
        const core::InspectOptions& inspectOptions = {});
};

}
