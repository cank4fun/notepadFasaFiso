#include "GuiDriverWindows.hpp"

#if defined(_WIN32)

#include "AutomationPipeClient.hpp"
#include "notepadFasaFiso/gui/automation/AutomationCommandLine.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace nff::gui::automation::windows {
namespace {

std::expected<std::wstring, DriverError> queryProcessPath(HANDLE process) {
    std::wstring buffer(32768U, L'\0');
    DWORD size = static_cast<DWORD>(buffer.size());
    if (::QueryFullProcessImageNameW(process, 0U, buffer.data(), &size) == FALSE || size == 0U) {
        return std::unexpected(DriverError::ProcessOpenFailed);
    }
    buffer.resize(static_cast<std::size_t>(size));
    return buffer;
}

bool exactExecutableName(const std::filesystem::path& path) {
    const auto filename = path.filename().wstring();
    return filename.size() == std::wstring_view(L"notepadFasaFiso_gui.exe").size() &&
           ::CompareStringOrdinal(filename.c_str(), static_cast<int>(filename.size()),
                                  L"notepadFasaFiso_gui.exe",
                                  static_cast<int>(std::wstring_view(L"notepadFasaFiso_gui.exe").size()),
                                  TRUE) == CSTR_EQUAL;
}

std::expected<std::string, DriverError> request(const std::uint32_t pid,
                                                const std::string_view value,
                                                const std::uint32_t timeoutMs = 3000U) {
    const auto response = sendAutomationRequest(pid, value, timeoutMs);
    if (!response) {
        return std::unexpected(DriverError::BridgeUnavailable);
    }
    if (response->starts_with("ERROR\t")) {
        if (*response == "ERROR\tnot_found") {
            return std::unexpected(DriverError::ElementNotFound);
        }
        return std::unexpected(DriverError::BridgeProtocolError);
    }
    return *response;
}

std::expected<void, DriverError> validateWindowOwnership(TargetSession& session, HWND hwnd) {
    if (hwnd == nullptr || !::IsWindow(hwnd)) {
        return std::unexpected(DriverError::WindowUnavailable);
    }
    DWORD ownerPid = 0U;
    static_cast<void>(::GetWindowThreadProcessId(hwnd, &ownerPid));
    if (ownerPid != session.pid()) {
        return std::unexpected(DriverError::InvalidTarget);
    }
    return {};
}

std::expected<void, DriverError> bringToForeground(TargetSession& session) {
    const auto window = queryWindow(session);
    if (!window) return std::unexpected(window.error());
    const auto hwnd = session.hwnd();
    if (::IsIconic(hwnd) != FALSE) {
        static_cast<void>(::ShowWindow(hwnd, SW_RESTORE));
    }
    if (::GetForegroundWindow() == hwnd) return {};
    static_cast<void>(::SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
    if (::SetForegroundWindow(hwnd) == FALSE) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        if (::SetForegroundWindow(hwnd) == FALSE && ::GetForegroundWindow() != hwnd) {
            return std::unexpected(DriverError::ForegroundFailed);
        }
    }
    return {};
}

std::expected<void, DriverError> validatePointInsideTarget(TargetSession& session,
                                                           const AutomationPoint point) {
    const auto window = queryWindow(session);
    if (!window) return std::unexpected(window.error());
    if (!pointInside(window->outerBounds, point)) {
        return std::unexpected(DriverError::ElementOutsideWindow);
    }
    return {};
}

bool sendMouseButton(const DWORD flag) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flag;
    return ::SendInput(1U, &input, sizeof(INPUT)) == 1U;
}

bool setCursor(const AutomationPoint point) {
    return ::SetCursorPos(point.x, point.y) != FALSE;
}

std::expected<std::wstring, DriverError> convertUtf8ToWide(const std::string_view value) {
    if (value.empty()) return std::wstring{};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(DriverError::InvalidText);
    }
    const auto bytes = static_cast<int>(value.size());
    const auto required = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                                 value.data(), bytes, nullptr, 0);
    if (required <= 0) return std::unexpected(DriverError::InvalidText);
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), bytes,
                              result.data(), required) != required) {
        return std::unexpected(DriverError::InvalidText);
    }
    return result;
}

std::vector<std::string> splitLines(const std::string_view value) {
    std::vector<std::string> lines;
    std::size_t begin = 0U;
    while (begin <= value.size()) {
        const auto end = value.find('\n', begin);
        auto line = end == std::string_view::npos ? value.substr(begin)
                                                  : value.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
        if (!line.empty()) lines.emplace_back(line);
        if (end == std::string_view::npos) break;
        begin = end + 1U;
    }
    return lines;
}

std::string upperAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

std::vector<std::string> splitChord(const std::string_view chord) {
    std::vector<std::string> parts;
    std::size_t begin = 0U;
    while (begin <= chord.size()) {
        const auto end = chord.find('+', begin);
        auto part = end == std::string_view::npos ? chord.substr(begin)
                                                  : chord.substr(begin, end - begin);
        if (part.empty()) return {};
        parts.push_back(upperAscii(std::string(part)));
        if (end == std::string_view::npos) break;
        begin = end + 1U;
    }
    return parts;
}

std::optional<WORD> primaryVirtualKey(const std::string& name) {
    if (name.size() == 1U) {
        const unsigned char ch = static_cast<unsigned char>(name[0]);
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            return static_cast<WORD>(ch);
        }
    }
    constexpr std::array<std::pair<std::string_view, WORD>, 19> keys{{
        {"ESC", VK_ESCAPE}, {"ESCAPE", VK_ESCAPE}, {"ENTER", VK_RETURN},
        {"RETURN", VK_RETURN}, {"TAB", VK_TAB}, {"SPACE", VK_SPACE},
        {"END", VK_END}, {"HOME", VK_HOME}, {"LEFT", VK_LEFT}, {"RIGHT", VK_RIGHT},
        {"UP", VK_UP}, {"DOWN", VK_DOWN}, {"DELETE", VK_DELETE},
        {"BACKSPACE", VK_BACK}, {"PAGEUP", VK_PRIOR}, {"PAGEDOWN", VK_NEXT},
        {"F5", VK_F5}, {"F11", VK_F11}, {"F12", VK_F12},
    }};
    const auto found = std::find_if(keys.begin(), keys.end(), [&](const auto& item) {
        return item.first == name;
    });
    return found != keys.end() ? std::optional<WORD>(found->second) : std::nullopt;
}

bool sendKey(const WORD key, const bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
    input.ki.dwFlags = down ? 0U : KEYEVENTF_KEYUP;
    return ::SendInput(1U, &input, sizeof(INPUT)) == 1U;
}

bool existingPrefixContainsSymlink(const std::filesystem::path& root,
                                   const std::filesystem::path& relative) {
    std::error_code error;
    auto current = root;
    for (const auto& part : relative) {
        current /= part;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error) {
            if (error == std::errc::no_such_file_or_directory) {
                error.clear();
                continue;
            }
            return true;
        }
        if (std::filesystem::is_symlink(status)) return true;
    }
    return false;
}

}

std::expected<std::wstring, DriverError> utf8ToWideText(const std::string_view value) {
    return convertUtf8ToWide(value);
}

std::string_view driverErrorText(const DriverError error) noexcept {
    switch (error) {
    case DriverError::InvalidTarget: return "invalid target process";
    case DriverError::ProcessOpenFailed: return "could not open target process";
    case DriverError::ProcessLaunchFailed: return "could not launch target process";
    case DriverError::BridgeUnavailable: return "automation bridge unavailable";
    case DriverError::BridgeProtocolError: return "automation bridge protocol error";
    case DriverError::WindowUnavailable: return "target window unavailable";
    case DriverError::ElementNotFound: return "semantic element not found";
    case DriverError::ElementUnavailable: return "semantic element is not actionable";
    case DriverError::ElementOutsideWindow: return "semantic element is outside the target window";
    case DriverError::ForegroundFailed: return "could not foreground target window";
    case DriverError::InputFailed: return "native input failed";
    case DriverError::InvalidText: return "invalid UTF-8 text";
    case DriverError::InvalidShortcut: return "invalid shortcut";
    case DriverError::WindowActionFailed: return "window action failed";
    case DriverError::Timeout: return "operation timed out";
    case DriverError::CaptureFailed: return "window capture failed";
    case DriverError::IoFailed: return "file I/O failed";
    case DriverError::UnsafePath: return "unsafe artifact path";
    }
    return "unknown driver error";
}

TargetSession::~TargetSession() {
    reset();
}

TargetSession::TargetSession(TargetSession&& other) noexcept
    : pid_(std::exchange(other.pid_, 0U)),
      process_(std::exchange(other.process_, nullptr)),
      hwnd_(std::exchange(other.hwnd_, nullptr)),
      executablePath_(std::move(other.executablePath_)) {}

TargetSession& TargetSession::operator=(TargetSession&& other) noexcept {
    if (this != &other) {
        reset();
        pid_ = std::exchange(other.pid_, 0U);
        process_ = std::exchange(other.process_, nullptr);
        hwnd_ = std::exchange(other.hwnd_, nullptr);
        executablePath_ = std::move(other.executablePath_);
    }
    return *this;
}

void TargetSession::reset() noexcept {
    if (process_ != nullptr && process_ != INVALID_HANDLE_VALUE) {
        ::CloseHandle(process_);
    }
    process_ = nullptr;
    hwnd_ = nullptr;
    pid_ = 0U;
    executablePath_.clear();
}

std::expected<TargetSession, DriverError>
launchTarget(const std::filesystem::path& executable,
             const std::vector<std::wstring>& appArguments,
             const std::uint32_t bridgeTimeoutMs) {
    if (!exactExecutableName(executable)) {
        return std::unexpected(DriverError::InvalidTarget);
    }
    std::error_code fsError;
    if (!std::filesystem::is_regular_file(executable, fsError) || fsError) {
        return std::unexpected(DriverError::InvalidTarget);
    }

    std::vector<std::wstring> commandArguments;
    commandArguments.reserve(appArguments.size() + 1U);
    commandArguments.push_back(executable.wstring());
    commandArguments.insert(commandArguments.end(), appArguments.begin(), appArguments.end());
    auto commandLine = buildWindowsCommandLine(commandArguments);
    commandLine.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION processInfo{};
    const auto workingDirectory = executable.parent_path().wstring();
    if (::CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0U,
                         nullptr, workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
                         &startup, &processInfo) == FALSE) {
        return std::unexpected(DriverError::ProcessLaunchFailed);
    }
    ::CloseHandle(processInfo.hThread);

    TargetSession session;
    session.pid_ = processInfo.dwProcessId;
    session.process_ = processInfo.hProcess;
    const auto path = queryProcessPath(session.process_);
    if (!path || !exactExecutableName(std::filesystem::path(*path))) {
        session.reset();
        return std::unexpected(DriverError::InvalidTarget);
    }
    session.executablePath_ = *path;

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(bridgeTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (::WaitForSingleObject(session.process_, 0U) == WAIT_OBJECT_0) {
            session.reset();
            return std::unexpected(DriverError::BridgeUnavailable);
        }
        const auto ping = request(session.pid_, "PING", 150U);
        if (ping && *ping == "PONG\t" + std::to_string(session.pid_)) {
            const auto window = queryWindow(session);
            if (window) return std::move(session);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    session.reset();
    return std::unexpected(DriverError::BridgeUnavailable);
}

std::expected<TargetSession, DriverError>
attachTarget(const std::uint32_t pid, const std::uint32_t bridgeTimeoutMs) {
    if (pid == 0U) return std::unexpected(DriverError::InvalidTarget);
    const auto process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (process == nullptr) return std::unexpected(DriverError::ProcessOpenFailed);

    const auto path = queryProcessPath(process);
    if (!path || !exactExecutableName(std::filesystem::path(*path))) {
        ::CloseHandle(process);
        return std::unexpected(DriverError::InvalidTarget);
    }

    TargetSession session;
    session.pid_ = pid;
    session.process_ = process;
    session.executablePath_ = *path;

    const auto ping = request(pid, "PING", bridgeTimeoutMs);
    if (!ping || *ping != "PONG\t" + std::to_string(pid)) {
        session.reset();
        return std::unexpected(DriverError::BridgeUnavailable);
    }
    const auto window = queryWindow(session);
    if (!window) {
        session.reset();
        return std::unexpected(window.error());
    }
    return std::move(session);
}

std::expected<AutomationWindowSnapshot, DriverError> queryWindow(TargetSession& session) {
    const auto response = request(session.pid(), "WINDOW");
    if (!response) return std::unexpected(response.error());
    const auto window = parseWindow(*response);
    if (!window || window->pid != session.pid()) {
        return std::unexpected(DriverError::BridgeProtocolError);
    }
    const auto hwndValue = static_cast<std::uintptr_t>(window->nativeHandle);
    const auto hwnd = reinterpret_cast<HWND>(hwndValue);
    const auto ownership = validateWindowOwnership(session, hwnd);
    if (!ownership) return std::unexpected(ownership.error());
    session.hwnd_ = hwnd;
    return *window;
}

std::expected<AutomationStatusSnapshot, DriverError> queryStatus(TargetSession& session) {
    const auto response = request(session.pid(), "STATUS");
    if (!response) return std::unexpected(response.error());
    const auto status = parseStatus(*response);
    return status ? std::expected<AutomationStatusSnapshot, DriverError>(*status)
                  : std::unexpected(DriverError::BridgeProtocolError);
}

std::expected<std::vector<AutomationElementSnapshot>, DriverError>
queryTree(TargetSession& session) {
    const auto response = request(session.pid(), "TREE");
    if (!response) return std::unexpected(response.error());
    std::vector<AutomationElementSnapshot> result;
    for (const auto& line : splitLines(*response)) {
        const auto element = parseElement(line);
        if (!element) return std::unexpected(DriverError::BridgeProtocolError);
        result.push_back(*element);
    }
    return result;
}

std::expected<AutomationElementSnapshot, DriverError>
queryElement(TargetSession& session, const std::string_view semanticId) {
    if (!isAddressableAutomationId(semanticId)) {
        return std::unexpected(DriverError::ElementNotFound);
    }
    const auto response = request(session.pid(), "GET " + std::string(semanticId));
    if (!response) return std::unexpected(response.error());
    const auto element = parseElement(*response);
    return element ? std::expected<AutomationElementSnapshot, DriverError>(*element)
                   : std::unexpected(DriverError::BridgeProtocolError);
}

std::expected<void, DriverError>
clickElement(TargetSession& session, const std::string_view semanticId) {
    const auto element = queryElement(session, semanticId);
    if (!element) return std::unexpected(element.error());
    if (!element->visible || !element->enabled || element->bounds.width <= 0 ||
        element->bounds.height <= 0) {
        return std::unexpected(DriverError::ElementUnavailable);
    }
    const auto foreground = bringToForeground(session);
    if (!foreground) return foreground;
    const auto point = centerPoint(element->bounds);
    const auto inside = validatePointInsideTarget(session, point);
    if (!inside) return inside;
    if (!setCursor(point) || !sendMouseButton(MOUSEEVENTF_LEFTDOWN) ||
        !sendMouseButton(MOUSEEVENTF_LEFTUP)) {
        return std::unexpected(DriverError::InputFailed);
    }
    return {};
}

std::expected<void, DriverError>
typeUnicode(TargetSession& session, const std::string_view utf8Text) {
    const auto foreground = bringToForeground(session);
    if (!foreground) return foreground;
    const auto text = convertUtf8ToWide(utf8Text);
    if (!text) return std::unexpected(text.error());

    for (const wchar_t unit : *text) {
        INPUT inputs[2]{};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wScan = static_cast<WORD>(unit);
        inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
        inputs[1] = inputs[0];
        inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        if (::SendInput(2U, inputs, sizeof(INPUT)) != 2U) {
            return std::unexpected(DriverError::InputFailed);
        }
    }
    return {};
}

std::expected<void, DriverError>
sendShortcut(TargetSession& session, const std::string_view chord) {
    const auto foreground = bringToForeground(session);
    if (!foreground) return foreground;
    const auto parts = splitChord(chord);
    if (parts.empty()) return std::unexpected(DriverError::InvalidShortcut);

    std::vector<WORD> modifiers;
    std::optional<WORD> primary;
    for (const auto& part : parts) {
        if (part == "CTRL" || part == "CONTROL") modifiers.push_back(VK_CONTROL);
        else if (part == "SHIFT") modifiers.push_back(VK_SHIFT);
        else if (part == "ALT") modifiers.push_back(VK_MENU);
        else if (part == "WIN" || part == "WINDOWS") modifiers.push_back(VK_LWIN);
        else {
            if (primary) return std::unexpected(DriverError::InvalidShortcut);
            primary = primaryVirtualKey(part);
            if (!primary) return std::unexpected(DriverError::InvalidShortcut);
        }
    }
    if (!primary) return std::unexpected(DriverError::InvalidShortcut);

    std::size_t pressed = 0U;
    for (; pressed < modifiers.size(); ++pressed) {
        if (!sendKey(modifiers[pressed], true)) break;
    }
    bool ok = pressed == modifiers.size();
    if (ok) ok = sendKey(*primary, true) && sendKey(*primary, false);
    while (pressed > 0U) {
        --pressed;
        ok = sendKey(modifiers[pressed], false) && ok;
    }
    return ok ? std::expected<void, DriverError>{}
              : std::unexpected(DriverError::InputFailed);
}

std::expected<void, DriverError>
dragElement(TargetSession& session,
            const std::string_view sourceId,
            const std::string_view targetId,
            const std::uint32_t durationMs) {
    const auto source = queryElement(session, sourceId);
    if (!source) return std::unexpected(source.error());
    const auto target = queryElement(session, targetId);
    if (!target) return std::unexpected(target.error());
    if (!source->visible || !source->enabled || !target->visible || !target->enabled) {
        return std::unexpected(DriverError::ElementUnavailable);
    }
    const auto from = centerPoint(source->bounds);
    const auto to = centerPoint(target->bounds);
    if (!validatePointInsideTarget(session, from) || !validatePointInsideTarget(session, to)) {
        return std::unexpected(DriverError::ElementOutsideWindow);
    }
    const auto foreground = bringToForeground(session);
    if (!foreground) return foreground;

    constexpr std::size_t segments = 12U;
    const auto points = interpolateDrag(from, to, segments);
    if (!setCursor(points.front()) || !sendMouseButton(MOUSEEVENTF_LEFTDOWN)) {
        return std::unexpected(DriverError::InputFailed);
    }
    const auto delay = std::chrono::milliseconds(std::max<std::uint32_t>(1U, durationMs / segments));
    bool ok = true;
    for (std::size_t index = 1U; index < points.size(); ++index) {
        std::this_thread::sleep_for(delay);
        if (!setCursor(points[index])) {
            ok = false;
            break;
        }
    }
    const bool released = sendMouseButton(MOUSEEVENTF_LEFTUP);
    return ok && released ? std::expected<void, DriverError>{}
                          : std::unexpected(DriverError::InputFailed);
}

std::expected<void, DriverError> maximizeWindow(TargetSession& session) {
    const auto window = queryWindow(session);
    if (!window) return std::unexpected(window.error());
    static_cast<void>(::ShowWindow(session.hwnd(), SW_MAXIMIZE));
    return {};
}

std::expected<void, DriverError> restoreWindow(TargetSession& session) {
    const auto window = queryWindow(session);
    if (!window) return std::unexpected(window.error());
    static_cast<void>(::ShowWindow(session.hwnd(), SW_RESTORE));
    return {};
}

std::expected<void, DriverError>
resizeWindow(TargetSession& session, const int x, const int y, const int width, const int height) {
    if (width <= 0 || height <= 0) return std::unexpected(DriverError::WindowActionFailed);
    const auto window = queryWindow(session);
    if (!window) return std::unexpected(window.error());
    if (::SetWindowPos(session.hwnd(), nullptr, x, y, width, height,
                       SWP_NOZORDER | SWP_NOACTIVATE) == FALSE) {
        return std::unexpected(DriverError::WindowActionFailed);
    }
    return {};
}

std::expected<void, DriverError> closeWindow(TargetSession& session) {
    const auto window = queryWindow(session);
    if (!window) return std::unexpected(window.error());
    if (::PostMessageW(session.hwnd(), WM_CLOSE, 0U, 0U) == FALSE) {
        return std::unexpected(DriverError::WindowActionFailed);
    }
    return {};
}

std::expected<void, DriverError>
waitForElementState(TargetSession& session,
                    const std::string_view semanticId,
                    const std::string_view state,
                    const std::uint32_t timeoutMs) {
    if (state != "visible" && state != "enabled" && state != "checked" && state != "hidden") {
        return std::unexpected(DriverError::ElementUnavailable);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        const auto element = queryElement(session, semanticId);
        if (element) {
            if (elementMatchesWaitState(std::optional<AutomationElementSnapshot>{*element}, state)) {
                return {};
            }
        } else if (element.error() == DriverError::ElementNotFound) {
            if (elementMatchesWaitState(std::nullopt, state)) {
                return {};
            }
        } else {
            return std::unexpected(element.error());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (std::chrono::steady_clock::now() < deadline);
    return std::unexpected(DriverError::Timeout);
}

std::expected<void, DriverError>
validateArtifactDestination(const std::filesystem::path& artifactRoot,
                            const std::filesystem::path& relative) {
    if (!isSafeRelativeArtifactPath(relative)) {
        return std::unexpected(DriverError::UnsafePath);
    }
    std::error_code error;
    std::filesystem::create_directories(artifactRoot, error);
    if (error) return std::unexpected(DriverError::IoFailed);
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(artifactRoot, error)) || error) {
        return std::unexpected(DriverError::UnsafePath);
    }
    if (existingPrefixContainsSymlink(artifactRoot, relative)) {
        return std::unexpected(DriverError::UnsafePath);
    }
    return {};
}

}

#endif
