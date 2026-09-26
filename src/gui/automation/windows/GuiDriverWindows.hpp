#pragma once

#if defined(_WIN32)

#include "notepadFasaFiso/gui/automation/AutomationProtocol.hpp"

#include <windows.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nff::gui::automation::windows {

enum class DriverError {
    InvalidTarget,
    ProcessOpenFailed,
    ProcessLaunchFailed,
    BridgeUnavailable,
    BridgeProtocolError,
    WindowUnavailable,
    ElementNotFound,
    ElementUnavailable,
    ElementOutsideWindow,
    ForegroundFailed,
    InputFailed,
    InvalidText,
    InvalidShortcut,
    WindowActionFailed,
    Timeout,
    CaptureFailed,
    IoFailed,
    UnsafePath,
};

[[nodiscard]] std::string_view driverErrorText(DriverError error) noexcept;
[[nodiscard]] std::expected<std::wstring, DriverError> utf8ToWideText(std::string_view value);

class TargetSession final {
public:
    TargetSession() = default;
    ~TargetSession();

    TargetSession(const TargetSession&) = delete;
    TargetSession& operator=(const TargetSession&) = delete;
    TargetSession(TargetSession&& other) noexcept;
    TargetSession& operator=(TargetSession&& other) noexcept;

    [[nodiscard]] std::uint32_t pid() const noexcept { return pid_; }
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }
    [[nodiscard]] HANDLE process() const noexcept { return process_; }
    [[nodiscard]] const std::wstring& executablePath() const noexcept { return executablePath_; }
    [[nodiscard]] bool valid() const noexcept {
        return pid_ != 0U && process_ != nullptr && process_ != INVALID_HANDLE_VALUE;
    }

private:
    friend std::expected<TargetSession, DriverError>
    launchTarget(const std::filesystem::path&, const std::vector<std::wstring>&, std::uint32_t);
    friend std::expected<TargetSession, DriverError> attachTarget(std::uint32_t, std::uint32_t);
    friend std::expected<AutomationWindowSnapshot, DriverError> queryWindow(TargetSession&);

    void reset() noexcept;

    std::uint32_t pid_{};
    HANDLE process_{};
    HWND hwnd_{};
    std::wstring executablePath_;
};

[[nodiscard]] std::expected<TargetSession, DriverError>
launchTarget(const std::filesystem::path& executable,
             const std::vector<std::wstring>& appArguments = {},
             std::uint32_t bridgeTimeoutMs = 10000U);

[[nodiscard]] std::expected<TargetSession, DriverError>
attachTarget(std::uint32_t pid, std::uint32_t bridgeTimeoutMs = 3000U);

[[nodiscard]] std::expected<AutomationWindowSnapshot, DriverError>
queryWindow(TargetSession& session);
[[nodiscard]] std::expected<AutomationStatusSnapshot, DriverError>
queryStatus(TargetSession& session);
[[nodiscard]] std::expected<std::vector<AutomationElementSnapshot>, DriverError>
queryTree(TargetSession& session);
[[nodiscard]] std::expected<AutomationElementSnapshot, DriverError>
queryElement(TargetSession& session, std::string_view semanticId);

[[nodiscard]] std::expected<void, DriverError>
clickElement(TargetSession& session, std::string_view semanticId);
[[nodiscard]] std::expected<void, DriverError>
typeUnicode(TargetSession& session, std::string_view utf8Text);
[[nodiscard]] std::expected<void, DriverError>
sendShortcut(TargetSession& session, std::string_view chord);
[[nodiscard]] std::expected<void, DriverError>
dragElement(TargetSession& session,
            std::string_view sourceId,
            std::string_view targetId,
            std::uint32_t durationMs);
[[nodiscard]] std::expected<void, DriverError> maximizeWindow(TargetSession& session);
[[nodiscard]] std::expected<void, DriverError> restoreWindow(TargetSession& session);
[[nodiscard]] std::expected<void, DriverError>
resizeWindow(TargetSession& session, int x, int y, int width, int height);
[[nodiscard]] std::expected<void, DriverError> closeWindow(TargetSession& session);

[[nodiscard]] std::expected<void, DriverError>
waitForElementState(TargetSession& session,
                    std::string_view semanticId,
                    std::string_view state,
                    std::uint32_t timeoutMs = 5000U);

[[nodiscard]] std::expected<void, DriverError>
validateArtifactDestination(const std::filesystem::path& artifactRoot,
                            const std::filesystem::path& relative);

}

#endif
