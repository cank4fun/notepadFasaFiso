#if defined(_WIN32)

#include "GuiDriverWindows.hpp"
#include "ScenarioRunner.hpp"
#include "WindowCapture.hpp"
#include "notepadFasaFiso/gui/automation/AutomationProtocol.hpp"

#include <windows.h>

#include <climits>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using nff::gui::automation::windows::DriverError;
using nff::gui::automation::windows::TargetSession;

void printHelp() {
    std::wcout
        << L"nff_gui_driver - notepadFasaFiso development GUI automation\n\n"
        << L"Usage:\n"
        << L"  nff_gui_driver run <scenario.nffgui> --app <exe> --artifact-root <dir> [-- app args...]\n"
        << L"  nff_gui_driver launch --app <exe> [-- app args...]\n"
        << L"  nff_gui_driver tree --pid <pid>\n"
        << L"  nff_gui_driver status --pid <pid>\n"
        << L"  nff_gui_driver click --pid <pid> <semantic-id>\n"
        << L"  nff_gui_driver type --pid <pid> <text>\n"
        << L"  nff_gui_driver shortcut --pid <pid> <keys>\n"
        << L"  nff_gui_driver drag --pid <pid> <source-id> <target-id> [--duration-ms N]\n"
        << L"  nff_gui_driver maximize|restore --pid <pid>\n"
        << L"  nff_gui_driver resize --pid <pid> <x> <y> <w> <h>\n"
        << L"  nff_gui_driver screenshot --pid <pid> <output.png>\n"
        << L"  nff_gui_driver snapshot --pid <pid> <output-dir>\n"
        << L"  nff_gui_driver wait --pid <pid> <semantic-id> <visible|enabled|checked|hidden>\n"
        << L"  nff_gui_driver close --pid <pid>\n";
}

std::optional<std::uint32_t> parsePid(const std::wstring_view value) {
    try {
        std::size_t consumed = 0U;
        const auto parsed = std::stoull(std::wstring(value), &consumed, 10);
        if (consumed != value.size() || parsed == 0U || parsed > UINT32_MAX) return std::nullopt;
        return static_cast<std::uint32_t>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<int> parseInt(const std::wstring_view value) {
    try {
        std::size_t consumed = 0U;
        const auto parsed = std::stoll(std::wstring(value), &consumed, 10);
        if (consumed != value.size() || parsed < INT_MIN || parsed > INT_MAX) return std::nullopt;
        return static_cast<int>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::uint32_t> parseU32(const std::wstring_view value) {
    const auto parsed = parsePid(value);
    return parsed;
}

std::optional<std::string> wideToUtf8(const std::wstring_view text) {
    if (text.empty()) return std::string{};
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return std::nullopt;
    const auto length = static_cast<int>(text.size());
    const auto required = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length,
                                                 nullptr, 0, nullptr, nullptr);
    if (required <= 0) return std::nullopt;
    std::string result(static_cast<std::size_t>(required), '\0');
    if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length,
                              result.data(), required, nullptr, nullptr) != required) {
        return std::nullopt;
    }
    return result;
}

int report(const std::expected<void, DriverError>& result) {
    if (result) return 0;
    std::cerr << "error: " << nff::gui::automation::windows::driverErrorText(result.error()) << '\n';
    return 2;
}

template <typename T>
int reportValue(const std::expected<T, DriverError>& result) {
    if (result) return 0;
    std::cerr << "error: " << nff::gui::automation::windows::driverErrorText(result.error()) << '\n';
    return 2;
}

std::expected<TargetSession, DriverError> attachFromArgs(const int argc,
                                                         wchar_t** argv,
                                                         const int startIndex,
                                                         int& nextIndex) {
    if (startIndex + 1 >= argc || std::wstring_view(argv[startIndex]) != L"--pid") {
        return std::unexpected(DriverError::InvalidTarget);
    }
    const auto pid = parsePid(argv[startIndex + 1]);
    if (!pid) return std::unexpected(DriverError::InvalidTarget);
    nextIndex = startIndex + 2;
    return nff::gui::automation::windows::attachTarget(*pid);
}

int runScenarioCommand(const int argc, wchar_t** argv) {
    if (argc < 3) {
        printHelp();
        return 2;
    }
    nff::gui::automation::driver::ScenarioRunOptions options;
    options.scenarioPath = std::filesystem::path(argv[2]);
    options.artifactRoot = options.scenarioPath.parent_path() / L"artifacts";

    int index = 3;
    while (index < argc) {
        const std::wstring_view arg(argv[index]);
        if (arg == L"--app") {
            if (++index >= argc) return 2;
            options.appPath = std::filesystem::path(argv[index++]);
            continue;
        }
        if (arg == L"--artifact-root") {
            if (++index >= argc) return 2;
            options.artifactRoot = std::filesystem::path(argv[index++]);
            continue;
        }
        if (arg == L"--") {
            ++index;
            for (; index < argc; ++index) options.appArguments.emplace_back(argv[index]);
            break;
        }
        std::wcerr << L"error: unknown run option: " << arg << L'\n';
        return 2;
    }

    const auto result = nff::gui::automation::driver::runScenario(options);
    if (!result) {
        std::cerr << "error: " << result.error() << '\n';
        return 2;
    }
    return 0;
}

int launchCommand(const int argc, wchar_t** argv) {
    if (argc < 4 || std::wstring_view(argv[2]) != L"--app") return 2;
    const std::filesystem::path app(argv[3]);
    std::vector<std::wstring> appArguments;
    int index = 4;
    if (index < argc && std::wstring_view(argv[index]) == L"--") ++index;
    for (; index < argc; ++index) appArguments.emplace_back(argv[index]);
    auto session = nff::gui::automation::windows::launchTarget(app, appArguments);
    if (!session) return reportValue(session);
    std::cout << "pid=" << session->pid() << '\n';
    return 0;
}

}

int wmain(const int argc, wchar_t** argv) {
    if (argc < 2 || std::wstring_view(argv[1]) == L"--help" ||
        std::wstring_view(argv[1]) == L"-h") {
        printHelp();
        return argc < 2 ? 2 : 0;
    }

    const std::wstring command(argv[1]);
    if (command == L"run") return runScenarioCommand(argc, argv);
    if (command == L"launch") return launchCommand(argc, argv);

    int next = 0;
    auto session = attachFromArgs(argc, argv, 2, next);
    if (!session) return reportValue(session);

    if (command == L"tree") {
        if (next != argc) return 2;
        const auto tree = nff::gui::automation::windows::queryTree(*session);
        if (!tree) return reportValue(tree);
        for (const auto& element : *tree) {
            std::cout << nff::gui::automation::serializeElement(element) << '\n';
        }
        return 0;
    }
    if (command == L"status") {
        if (next != argc) return 2;
        const auto status = nff::gui::automation::windows::queryStatus(*session);
        if (!status) return reportValue(status);
        std::cout << nff::gui::automation::serializeStatus(*status) << '\n';
        return 0;
    }
    if (command == L"click") {
        if (next + 1 != argc) return 2;
        const auto id = wideToUtf8(argv[next]);
        if (!id) return 2;
        return report(nff::gui::automation::windows::clickElement(*session, *id));
    }
    if (command == L"type") {
        if (next + 1 != argc) return 2;
        const auto text = wideToUtf8(argv[next]);
        if (!text) return 2;
        return report(nff::gui::automation::windows::typeUnicode(*session, *text));
    }
    if (command == L"shortcut") {
        if (next + 1 != argc) return 2;
        const auto chord = wideToUtf8(argv[next]);
        if (!chord) return 2;
        return report(nff::gui::automation::windows::sendShortcut(*session, *chord));
    }
    if (command == L"drag") {
        if (next + 2 > argc) return 2;
        const auto source = wideToUtf8(argv[next++]);
        const auto target = next < argc ? wideToUtf8(argv[next++]) : std::nullopt;
        if (!source || !target) return 2;
        std::uint32_t duration = 200U;
        if (next < argc) {
            if (next + 2 != argc || std::wstring_view(argv[next]) != L"--duration-ms") return 2;
            const auto parsed = parseU32(argv[next + 1]);
            if (!parsed) return 2;
            duration = *parsed;
            next += 2;
        }
        if (next != argc) return 2;
        return report(nff::gui::automation::windows::dragElement(*session, *source, *target, duration));
    }
    if (command == L"maximize") {
        if (next != argc) return 2;
        return report(nff::gui::automation::windows::maximizeWindow(*session));
    }
    if (command == L"restore") {
        if (next != argc) return 2;
        return report(nff::gui::automation::windows::restoreWindow(*session));
    }
    if (command == L"resize") {
        if (next + 4 != argc) return 2;
        const auto x = parseInt(argv[next]);
        const auto y = parseInt(argv[next + 1]);
        const auto width = parseInt(argv[next + 2]);
        const auto height = parseInt(argv[next + 3]);
        if (!x || !y || !width || !height) return 2;
        return report(nff::gui::automation::windows::resizeWindow(*session, *x, *y, *width, *height));
    }
    if (command == L"screenshot") {
        if (next + 1 != argc) return 2;
        return report(nff::gui::automation::driver::writeScreenshot(*session,
                                                                     std::filesystem::path(argv[next])));
    }
    if (command == L"snapshot") {
        if (next + 1 != argc) return 2;
        const std::filesystem::path output(argv[next]);
        const auto parent = output.parent_path().empty() ? std::filesystem::current_path()
                                                         : output.parent_path();
        const auto relative = output.filename();
        return report(nff::gui::automation::driver::writeSnapshot(*session, parent, relative));
    }
    if (command == L"wait") {
        if (next + 2 != argc) return 2;
        const auto id = wideToUtf8(argv[next]);
        const auto state = wideToUtf8(argv[next + 1]);
        if (!id || !state) return 2;
        return report(nff::gui::automation::windows::waitForElementState(*session, *id, *state));
    }
    if (command == L"close") {
        if (next != argc) return 2;
        return report(nff::gui::automation::windows::closeWindow(*session));
    }

    printHelp();
    return 2;
}

#else

int main() { return 2; }

#endif
