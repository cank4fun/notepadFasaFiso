#pragma once

#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/recovery/RecoveryManager.hpp"
#include "notepadFasaFiso/workspace/WorkspaceModel.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <system_error>
#include <vector>

namespace nff::session {

struct SessionDocumentState final {
    core::DocumentId id{};
    std::filesystem::path path;
    std::filesystem::path recoverySnapshot;
    bool modified{false};
    bool untitled{false};
};

struct SessionState final {
    std::vector<SessionDocumentState> documents;
    workspace::WorkspaceSnapshot workspace;
};

struct SessionLoadResult final {
    SessionState state;
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

class SessionStore final {
public:
    static constexpr std::uint32_t currentSchemaVersion = 2U;
    static constexpr std::size_t defaultMaximumSessionBytes = 64U * 1024U * 1024U;

    [[nodiscard]] static SessionState capture(
        const core::DocumentManager& documents,
        const workspace::WorkspaceModel& workspace,
        const recovery::RecoveryManager* recovery = nullptr);
    [[nodiscard]] static std::error_code save(const std::filesystem::path& path,
                                              const SessionState& state);
    [[nodiscard]] static SessionLoadResult load(
        const std::filesystem::path& path,
        std::size_t maximumBytes = defaultMaximumSessionBytes);
};

}
