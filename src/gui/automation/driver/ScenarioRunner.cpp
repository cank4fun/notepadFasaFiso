#include "ScenarioRunner.hpp"

#if defined(_WIN32)

#include <windows.h>

#include "WindowCapture.hpp"
#include "notepadFasaFiso/gui/automation/AutomationCommandLine.hpp"
#include "notepadFasaFiso/gui/automation/AutomationScenario.hpp"

#include <chrono>
#include <filesystem>
#include <climits>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace nff::gui::automation::driver {
namespace {

std::expected<std::string, std::string> readUtf8File(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::unexpected("could not open scenario file");
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) {
        return std::unexpected("could not read scenario file");
    }
    return buffer.str();
}

std::expected<void, windows::DriverError>
writeTextTemp(const std::filesystem::path& path, const std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return std::unexpected(windows::DriverError::IoFailed);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    if (!output) return std::unexpected(windows::DriverError::IoFailed);
    return {};
}

std::expected<void, windows::DriverError>
replaceFile(const std::filesystem::path& temporary, const std::filesystem::path& finalPath) {
    std::error_code error;
    std::filesystem::remove(finalPath, error);
    error.clear();
    std::filesystem::rename(temporary, finalPath, error);
    if (error) return std::unexpected(windows::DriverError::IoFailed);
    return {};
}

std::string treeText(const std::vector<AutomationElementSnapshot>& elements) {
    std::string result;
    for (const auto& element : elements) {
        if (!result.empty()) result.push_back('\n');
        result += serializeElement(element);
    }
    if (!result.empty()) result.push_back('\n');
    return result;
}

std::string describeState(windows::TargetSession& session) {
    std::string result;
    if (const auto window = windows::queryWindow(session); window) {
        result += " window=" + serializeWindow(*window);
    }
    if (const auto status = windows::queryStatus(session); status) {
        result += " status=" + serializeStatus(*status);
    }
    return result;
}

std::expected<std::uint32_t, std::string> parsePid(const std::string& value) {
    try {
        std::size_t consumed = 0U;
        const auto parsed = std::stoull(value, &consumed, 10);
        if (consumed != value.size() || parsed == 0U || parsed > UINT32_MAX) {
            return std::unexpected("invalid pid");
        }
        return static_cast<std::uint32_t>(parsed);
    } catch (...) {
        return std::unexpected("invalid pid");
    }
}

std::expected<int, std::string> parseInt(const std::string& value) {
    try {
        std::size_t consumed = 0U;
        const auto parsed = std::stoll(value, &consumed, 10);
        if (consumed != value.size() || parsed < INT_MIN || parsed > INT_MAX) {
            return std::unexpected("invalid integer");
        }
        return static_cast<int>(parsed);
    } catch (...) {
        return std::unexpected("invalid integer");
    }
}

std::expected<std::filesystem::path, std::string> utf8Path(const std::string& value) {
    const auto wide = windows::utf8ToWideText(value);
    if (!wide) return std::unexpected("invalid UTF-8 path");
    return std::filesystem::path(*wide);
}

std::expected<std::vector<std::wstring>, std::string>
utf8Arguments(const std::vector<std::string>& values, const std::size_t begin) {
    std::vector<std::wstring> result;
    result.reserve(values.size() - begin);
    for (std::size_t index = begin; index < values.size(); ++index) {
        if (values[index] == "--" && index == begin) continue;
        const auto wide = windows::utf8ToWideText(values[index]);
        if (!wide) return std::unexpected("invalid UTF-8 launch argument");
        result.push_back(*wide);
    }
    return result;
}

std::expected<std::filesystem::path, std::string> freshAutomationStateRoot() {
    std::error_code error;
    const auto temporary = std::filesystem::temp_directory_path(error);
    if (error || temporary.empty()) {
        return std::unexpected("could not resolve automation temporary directory");
    }
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = temporary / "notepadFasaFiso-gui-automation" /
                      (std::to_string(::GetCurrentProcessId()) + "-" + std::to_string(ticks));
    std::filesystem::create_directories(root, error);
    if (error) {
        return std::unexpected("could not create isolated automation state root");
    }
    return root;
}

std::expected<std::vector<std::wstring>, std::string>
isolatedLaunchArguments(const std::vector<std::wstring>& arguments,
                        const std::filesystem::path& stateRoot) {
    return withAutomationStateRootArgument(arguments, stateRoot.wstring());
}

std::expected<void, std::string>
executeCommand(const ScenarioCommand& command,
               const std::filesystem::path& artifactRoot,
               const std::filesystem::path& automationStateRoot,
               std::optional<windows::TargetSession>& session) {
    auto requireSession = [&]() -> std::expected<windows::TargetSession*, std::string> {
        if (!session || !session->valid()) {
            return std::unexpected("scenario command requires a launched/attached application");
        }
        return &*session;
    };
    auto fromDriver = [&](const std::expected<void, windows::DriverError>& result)
        -> std::expected<void, std::string> {
        if (!result) return std::unexpected(std::string(windows::driverErrorText(result.error())));
        return {};
    };

    switch (command.kind) {
    case ScenarioCommandKind::Launch: {
        if (session) return std::unexpected("scenario attempted to launch a second target");
        if (command.arguments.empty()) return std::unexpected("launch path is missing");
        const auto path = utf8Path(command.arguments[0]);
        if (!path) return std::unexpected(path.error());
        const auto args = utf8Arguments(command.arguments, 1U);
        if (!args) return std::unexpected(args.error());
        const auto isolatedArgs = isolatedLaunchArguments(*args, automationStateRoot);
        if (!isolatedArgs) return std::unexpected(isolatedArgs.error());
        auto launched = windows::launchTarget(*path, *isolatedArgs);
        if (!launched) return std::unexpected(std::string(windows::driverErrorText(launched.error())));
        session.emplace(std::move(*launched));
        return {};
    }
    case ScenarioCommandKind::Attach: {
        if (session) return std::unexpected("scenario attempted to attach a second target");
        const auto pid = parsePid(command.arguments.at(0));
        if (!pid) return std::unexpected(pid.error());
        auto attached = windows::attachTarget(*pid);
        if (!attached) return std::unexpected(std::string(windows::driverErrorText(attached.error())));
        session.emplace(std::move(*attached));
        return {};
    }
    case ScenarioCommandKind::Tree: {
        const auto target = requireSession();
        if (!target) return std::unexpected(target.error());
        const auto tree = windows::queryTree(**target);
        if (!tree) return std::unexpected(std::string(windows::driverErrorText(tree.error())));
        std::cout << treeText(*tree);
        return {};
    }
    case ScenarioCommandKind::Status: {
        const auto target = requireSession();
        if (!target) return std::unexpected(target.error());
        const auto status = windows::queryStatus(**target);
        if (!status) return std::unexpected(std::string(windows::driverErrorText(status.error())));
        std::cout << serializeStatus(*status) << '\n';
        return {};
    }
    case ScenarioCommandKind::Click: {
        const auto target = requireSession();
        if (!target) return std::unexpected(target.error());
        return fromDriver(windows::clickElement(**target, command.arguments.at(0)));
    }
    case ScenarioCommandKind::Type: {
        const auto target = requireSession();
        if (!target) return std::unexpected(target.error());
        return fromDriver(windows::typeUnicode(**target, command.arguments.at(0)));
    }
    case ScenarioCommandKind::Shortcut: {
        const auto target = requireSession();
        if (!target) return std::unexpected(target.error());
        return fromDriver(windows::sendShortcut(**target, command.arguments.at(0)));
    }
    case ScenarioCommandKind::Drag: {
        const auto target = requireSession();
        if (!target) return std::unexpected(target.error());
        return fromDriver(windows::dragElement(**target, command.arguments.at(0),
                                               command.arguments.at(1), command.durationMs));
    }
    case ScenarioCommandKind::Maximize: {
        const auto target = requireSession();
        if (!target) return std::unexpected(target.error());
        return fromDriver(windows::maximizeWindow(**target));
    }
    case ScenarioCommandKind::Restore: {
        const auto target = requireSession();
        if (!target) return std::unexpected(target.error());
        return fromDriver(windows::restoreWindow(**target));
    }
    case ScenarioCommandKind::Resize: {
        const auto target = requireSession();
        if (!target) return std::unexpected(target.error());
        const auto x = parseInt(command.arguments.at(0));
        const auto y = parseInt(command.arguments.at(1));
        const auto width = parseInt(command.arguments.at(2));
        const auto height = parseInt(command.arguments.at(3));
        if (!x || !y || !width || !height) return std::unexpected("invalid resize arguments");
        return fromDriver(windows::resizeWindow(**target, *x, *y, *width, *height));
    }
    case ScenarioCommandKind::Screenshot: {
        const auto target = requireSession();
        if (!target) return std::unexpected(target.error());
        const auto relativePath = utf8Path(command.arguments.at(0));
        if (!relativePath) return std::unexpected(relativePath.error());
        const auto relative = *relativePath;
        if (!isSafeRelativeArtifactPath(relative)) return std::unexpected("unsafe screenshot path");
        const auto validation = windows::validateArtifactDestination(artifactRoot, relative.parent_path().empty()
            ? std::filesystem::path(".") : relative.parent_path());
        if (!validation) return std::unexpected(std::string(windows::driverErrorText(validation.error())));
        std::error_code error;
        std::filesystem::create_directories((artifactRoot / relative).parent_path(), error);
        if (error) return std::unexpected("could not create screenshot directory");
        return fromDriver(writeScreenshot(**target, artifactRoot / relative));
    }
    case ScenarioCommandKind::Snapshot: {
        const auto target = requireSession();
        if (!target) return std::unexpected(target.error());
        const auto relative = utf8Path(command.arguments.at(0));
        if (!relative) return std::unexpected(relative.error());
        return fromDriver(writeSnapshot(**target, artifactRoot, *relative));
    }
    case ScenarioCommandKind::Wait: {
        const auto target = requireSession();
        if (!target) return std::unexpected(target.error());
        return fromDriver(windows::waitForElementState(**target, command.arguments.at(0),
                                                       command.arguments.at(1)));
    }
    case ScenarioCommandKind::Close: {
        const auto target = requireSession();
        if (!target) return std::unexpected(target.error());
        return fromDriver(windows::closeWindow(**target));
    }
    }
    return std::unexpected("unsupported scenario command");
}

}

std::expected<void, windows::DriverError>
writeScreenshot(windows::TargetSession& session, const std::filesystem::path& outputPath) {
    const auto window = windows::queryWindow(session);
    if (!window) return std::unexpected(window.error());
    std::error_code error;
    const auto parent = outputPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) return std::unexpected(windows::DriverError::IoFailed);
    }
    return windows::captureWindowPng(session.hwnd(), outputPath);
}

std::expected<void, windows::DriverError>
writeSnapshot(windows::TargetSession& session,
              const std::filesystem::path& artifactRoot,
              const std::filesystem::path& relativeDirectory) {
    const auto validation = windows::validateArtifactDestination(artifactRoot, relativeDirectory);
    if (!validation) return validation;

    const auto directory = artifactRoot / relativeDirectory;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return std::unexpected(windows::DriverError::IoFailed);

    const auto tree = windows::queryTree(session);
    if (!tree) return std::unexpected(tree.error());
    const auto status = windows::queryStatus(session);
    if (!status) return std::unexpected(status.error());
    const auto window = windows::queryWindow(session);
    if (!window) return std::unexpected(window.error());

    const auto screenshotTemp = directory / ".screenshot.png.tmp";
    const auto treeTemp = directory / ".tree.tsv.tmp";
    const auto statusTemp = directory / ".status.txt.tmp";
    const auto windowTemp = directory / ".window.txt.tmp";

    for (const auto& temp : {screenshotTemp, treeTemp, statusTemp, windowTemp}) {
        std::filesystem::remove(temp, error);
        error.clear();
    }

    auto capture = windows::captureWindowPng(session.hwnd(), screenshotTemp);
    if (!capture) return capture;
    if (auto written = writeTextTemp(treeTemp, treeText(*tree)); !written) return written;
    if (auto written = writeTextTemp(statusTemp, serializeStatus(*status) + "\n"); !written) return written;

    const auto epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string windowText = serializeWindow(*window) + "\n";
    windowText += "captured_epoch_ms=" + std::to_string(epochMs) + "\n";
    if (auto written = writeTextTemp(windowTemp, windowText); !written) return written;

    const std::pair<std::filesystem::path, std::filesystem::path> files[] = {
        {screenshotTemp, directory / "screenshot.png"},
        {treeTemp, directory / "tree.tsv"},
        {statusTemp, directory / "status.txt"},
        {windowTemp, directory / "window.txt"},
    };
    for (const auto& [temp, finalPath] : files) {
        if (auto moved = replaceFile(temp, finalPath); !moved) return moved;
    }
    return {};
}

std::expected<void, std::string> runScenario(const ScenarioRunOptions& options) {
    const auto text = readUtf8File(options.scenarioPath);
    if (!text) return std::unexpected(text.error());
    const auto parsed = parseScenario(*text);
    if (!parsed) {
        return std::unexpected("scenario parse error at line " +
                               std::to_string(parsed.error().line) + ": " +
                               parsed.error().message);
    }

    const auto automationStateRoot = freshAutomationStateRoot();
    if (!automationStateRoot) {
        return std::unexpected(automationStateRoot.error());
    }

    std::optional<windows::TargetSession> session;
    if (options.appPath) {
        const auto isolatedArgs =
            isolatedLaunchArguments(options.appArguments, *automationStateRoot);
        if (!isolatedArgs) return std::unexpected(isolatedArgs.error());
        auto launched = windows::launchTarget(*options.appPath, *isolatedArgs);
        if (!launched) {
            return std::unexpected(std::string(windows::driverErrorText(launched.error())));
        }
        std::cout << "pid=" << launched->pid() << '\n';
        session.emplace(std::move(*launched));
    }

    for (const auto& command : parsed->commands) {
        auto result = executeCommand(command, options.artifactRoot,
                                     *automationStateRoot, session);
        if (!result) {
            std::string message = "scenario line " + std::to_string(command.line) + " failed: " +
                                  result.error();
            if (session && session->valid()) message += describeState(*session);
            return std::unexpected(std::move(message));
        }
    }
    return {};
}

}

#endif
