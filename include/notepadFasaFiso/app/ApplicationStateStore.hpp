#pragma once

#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/history/RecentFiles.hpp"
#include "notepadFasaFiso/recovery/RecoveryManager.hpp"
#include "notepadFasaFiso/session/SessionStore.hpp"
#include "notepadFasaFiso/settings/AppSettings.hpp"
#include "notepadFasaFiso/workspace/WorkspaceModel.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <system_error>
#include <vector>

namespace nff::app {

struct ApplicationPaths final {
    std::filesystem::path root;
    std::filesystem::path settings;
    std::filesystem::path session;
    std::filesystem::path recentFiles;
    std::filesystem::path recoveryDirectory;
    std::filesystem::path metadataDirectory;

    [[nodiscard]] static ApplicationPaths under(std::filesystem::path root);
    [[nodiscard]] static ApplicationPaths systemDefault();
};

enum class PersistentStateArea : std::uint8_t {
    Settings,
    Session,
    RecentFiles,
    Recovery,
};

struct PersistentStateIssue final {
    PersistentStateArea area{PersistentStateArea::Settings};
    std::filesystem::path path;
    std::error_code error;
};

struct StartupPersistentState final {
    settings::AppSettings settings{};
    history::RecentFiles recentFiles{};
    std::optional<session::SessionState> session{};
    std::vector<std::filesystem::path> recoverySnapshots;
    std::vector<PersistentStateIssue> issues;
};

struct PersistentSaveResult final {
    std::vector<PersistentStateIssue> issues;

    [[nodiscard]] explicit operator bool() const noexcept { return issues.empty(); }
};

class ApplicationStateStore final {
public:
    explicit ApplicationStateStore(ApplicationPaths paths);

    [[nodiscard]] const ApplicationPaths& paths() const noexcept;
    [[nodiscard]] StartupPersistentState load() const;

    [[nodiscard]] PersistentSaveResult save(
        const settings::AppSettings& settings,
        const history::RecentFiles& recentFiles,
        const core::DocumentManager& documents,
        const workspace::WorkspaceModel& workspace,
        const recovery::RecoveryManager* recovery = nullptr) const;

private:
    [[nodiscard]] static bool regularFileExists(const std::filesystem::path& path) noexcept;

    ApplicationPaths paths_;
};

}
