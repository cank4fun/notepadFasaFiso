#include "notepadFasaFiso/gui/automation/AutomationProtocol.hpp"
#include "notepadFasaFiso/gui/automation/AutomationScenario.hpp"
#include "notepadFasaFiso/gui/automation/AutomationCommandLine.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void testProtocolRoundTrip() {
    using namespace nff::gui::automation;

    const std::string field = "tab\tname\\x\nline\r";
    const auto encoded = encodeField(field);
    const auto decoded = decodeField(encoded);
    expect(decoded.has_value() && *decoded == field,
           "automation protocol fields round-trip");

    const AutomationElementSnapshot original{
        .id = "tab.42",
        .role = "tab",
        .bounds = AutomationRect{10, 20, 130, 32},
        .visible = true,
        .enabled = true,
        .checked = false,
        .text = "notes.txt",
    };
    const auto parsed = parseElement(serializeElement(original));
    expect(parsed.has_value() && *parsed == original,
           "semantic element snapshots round-trip");
}

void testProtocolRejectsMalformedInput() {
    using namespace nff::gui::automation;

    expect(!decodeField("bad\\qescape").has_value(),
           "unknown field escapes are rejected");
    expect(!parseElement("ELEMENT\tbad id\ttab\t0\t0\t10\t10\t1\t1\t0\ttext").has_value(),
           "malformed semantic ids are rejected in snapshots");
    expect(!parseElement("ELEMENT\ttab.1\ttab\t0\t0\t-1\t10\t1\t1\t0\ttext").has_value(),
           "negative snapshot dimensions are rejected");
}

void testGeometry() {
    using namespace nff::gui::automation;

    const AutomationRect source{100, 100, 80, 30};
    const AutomationRect target{500, 300, 120, 40};
    const auto sourceCenter = centerPoint(source);
    const auto targetCenter = centerPoint(target);
    const auto points = interpolateDrag(sourceCenter, targetCenter, 8U);

    expect(points.size() == 9U, "drag includes both endpoints");
    expect(!points.empty() && points.front() == sourceCenter,
           "drag starts at source center");
    expect(!points.empty() && points.back() == targetCenter,
           "drag ends at target center");

    const auto minX = std::min(sourceCenter.x, targetCenter.x);
    const auto maxX = std::max(sourceCenter.x, targetCenter.x);
    const auto minY = std::min(sourceCenter.y, targetCenter.y);
    const auto maxY = std::max(sourceCenter.y, targetCenter.y);
    expect(std::all_of(points.begin(), points.end(), [&](const AutomationPoint point) {
               return point.x >= minX && point.x <= maxX && point.y >= minY &&
                      point.y <= maxY;
           }),
           "drag interpolation remains between endpoints");
}

void testSemanticIdAndExecutableValidation() {
    using namespace nff::gui::automation;

    expect(isValidSemanticId("pane.7"), "dynamic pane ids are valid");
    expect(isValidSemanticId("tab.42"), "dynamic tab ids are valid");
    expect(isValidSemanticId("editor.42"), "dynamic editor ids are valid");
    expect(!isValidSemanticId(""), "empty semantic ids are invalid");
    expect(!isValidSemanticId("../window.main"), "path traversal semantic ids are invalid");
    expect(!isValidSemanticId("bad id"), "whitespace semantic ids are invalid");
    expect(!isValidSemanticId("bad\\id"), "backslash semantic ids are invalid");

    expect(isAllowedTargetExecutableName("notepadFasaFiso_gui.exe"),
           "exact GUI executable name is allowed");
    expect(isAllowedTargetExecutableName("NOTEPADFASAFISO_GUI.EXE"),
           "GUI executable validation is case-insensitive");
    expect(!isAllowedTargetExecutableName("explorer.exe"),
           "Explorer is rejected");
    expect(!isAllowedTargetExecutableName("cmd.exe"), "cmd is rejected");
    expect(!isAllowedTargetExecutableName("notepadFasaFiso_gui.exe.bak"),
           "GUI executable lookalikes are rejected");

    expect(isAddressableAutomationId("menu.view"),
           "declared static semantic ids are addressable");
    expect(isAddressableAutomationId("pane.7") && isAddressableAutomationId("tab.42") &&
               isAddressableAutomationId("editor.42") &&
               isAddressableAutomationId("splitter.3"),
           "dynamic workspace semantic ids are addressable");
    expect(isAddressableAutomationId("dialog.settings") &&
               isAddressableAutomationId("settings.ok") &&
               isAddressableAutomationId("find.pattern"),
           "dialog semantic namespaces are addressable");
    expect(!isAddressableAutomationId("button") &&
               !isAddressableAutomationId("panel") &&
               !isAddressableAutomationId("text"),
           "wx default control names are excluded from the semantic tree");
}

void testSemanticTextEnrichment() {
    using namespace nff::gui::automation;

    std::vector<AutomationElementSnapshot> elements{
        {.id = "tab.1", .role = "tab", .bounds = {0, 0, 100, 24},
         .visible = true, .enabled = true, .checked = false, .text = {}},
        {.id = "tab.2", .role = "tab", .bounds = {100, 0, 100, 24},
         .visible = true, .enabled = true, .checked = false, .text = {}},
    };

    expect(setElementText(elements, "tab.1", "Untitled 1"),
           "semantic text enrichment finds an existing element");
    expect(elements[0].text == "Untitled 1",
           "semantic text enrichment publishes the visible tab title");
    expect(!setElementText(elements, "tab.99", "missing"),
           "semantic text enrichment reports missing semantic ids");
}

void testRequiredStaticIds() {
    using namespace nff::gui::automation;
    const auto ids = requiredStaticSemanticIds();
    const std::vector<std::string_view> required{
        "window.main",        "menu.file",          "menu.edit",
        "menu.view",          "menu.format",        "sidebar.search",
        "sidebar.tree",       "sidebar.splitter",   "notice.external",
        "notice.reload",      "notice.save_mine",   "notice.save_as",
        "status.position",    "status.encoding",    "status.line_ending",
        "status.format",      "status.mode",        "status.extra",
    };
    for (const auto id : required) {
        expect(std::find(ids.begin(), ids.end(), id) != ids.end(),
               std::string("required static semantic id exists: ") + std::string(id));
    }
}

void testWindowsCommandLineQuoting() {
    using namespace nff::gui::automation;
    expect(quoteWindowsArgument(L"plain.txt") == L"plain.txt",
           "plain Windows argument does not need quotes");
    expect(quoteWindowsArgument(L"") == L"\"\"",
           "empty Windows argument is explicitly quoted");
    expect(quoteWindowsArgument(L"two words.txt") == L"\"two words.txt\"",
           "Windows argument with spaces is quoted");
    expect(quoteWindowsArgument(L"a\\\"b") == L"\"a\\\\\\\"b\"",
           "backslashes before quotes are doubled and quote escaped");
    expect(quoteWindowsArgument(L"C:\\path with space\\") ==
               L"\"C:\\path with space\\\\\"",
           "trailing backslashes are doubled before closing quote");

    const std::vector<std::wstring> args{L"C:\\Apps\\notepadFasaFiso_gui.exe",
                                         L"C:\\a file.txt", L"--flag"};
    expect(buildWindowsCommandLine(args) ==
               L"C:\\Apps\\notepadFasaFiso_gui.exe \"C:\\a file.txt\" --flag",
           "Windows command line joins individually quoted arguments");
}

void testAutomationStateRootArgumentExtraction() {
    using namespace nff::gui::automation;

    std::vector<std::string> arguments{
        "--view", "notes.txt", "--nff-automation-state-root", "C:/Temp/nff-state", "--edit"};
    const auto extracted = extractAutomationStateRootArgument(arguments);
    expect(extracted.has_value() && extracted->has_value() &&
               **extracted == "C:/Temp/nff-state",
           "automation state root is extracted");
    expect(arguments == std::vector<std::string>({"--view", "notes.txt", "--edit"}),
           "automation-only state-root arguments are removed before normal CLI parsing");

    std::vector<std::string> untouched{"--view", "notes.txt"};
    const auto absent = extractAutomationStateRootArgument(untouched);
    expect(absent.has_value() && !absent->has_value(),
           "missing automation state root leaves normal app arguments unchanged");
    expect(untouched == std::vector<std::string>({"--view", "notes.txt"}),
           "normal app arguments remain unchanged");

    std::vector<std::string> missingValue{"--nff-automation-state-root"};
    expect(!extractAutomationStateRootArgument(missingValue).has_value(),
           "automation state root requires a value");

    std::vector<std::string> duplicate{
        "--nff-automation-state-root", "one", "--nff-automation-state-root", "two"};
    expect(!extractAutomationStateRootArgument(duplicate).has_value(),
           "duplicate automation state roots are rejected");

    std::vector<std::string> emptyValue{"--nff-automation-state-root", ""};
    expect(!extractAutomationStateRootArgument(emptyValue).has_value(),
           "empty automation state root is rejected");
}

void testAutomationStateRootLaunchArgumentInjection() {
    using namespace nff::gui::automation;

    const std::vector<std::wstring> original{L"--view", L"C:\\logs\\a file.txt"};
    const auto injected = withAutomationStateRootArgument(original, L"C:\\Temp\\nff-state");
    expect(injected.has_value() &&
               *injected == std::vector<std::wstring>({
                   L"--view", L"C:\\logs\\a file.txt",
                   L"--nff-automation-state-root", L"C:\\Temp\\nff-state"}),
           "scenario launch arguments carry an isolated automation state root");
    expect(!withAutomationStateRootArgument(original, L"").has_value(),
           "empty automation state root is rejected before launch");
}

void testArtifactRelativePathValidation() {
    using namespace nff::gui::automation;
    expect(isSafeRelativeArtifactPath(std::filesystem::path("settings/before")),
           "nested relative artifact path is safe");
    expect(!isSafeRelativeArtifactPath(std::filesystem::path("../outside")),
           "artifact traversal is rejected");
    expect(!isSafeRelativeArtifactPath(std::filesystem::path("C:/outside")),
           "drive-qualified artifact path is rejected");
    expect(!isSafeRelativeArtifactPath(std::filesystem::path("/outside")),
           "absolute artifact path is rejected");
}

void testBridgeCommandProtocol() {
    using namespace nff::gui::automation;

    const auto ping = parseBridgeCommand("PING");
    expect(ping.has_value() && ping->kind == BridgeCommandKind::Ping,
           "bridge accepts PING");
    const auto get = parseBridgeCommand("GET tab.42");
    expect(get.has_value() && get->kind == BridgeCommandKind::Get && get->semanticId == "tab.42",
           "bridge GET carries a validated semantic id");
    expect(!parseBridgeCommand("GET bad/id").has_value(),
           "bridge GET rejects invalid semantic ids");
    expect(!parseBridgeCommand("CLICK menu.view").has_value(),
           "read-only bridge rejects input actions");
    expect(!parseBridgeCommand("SHELL calc.exe").has_value(),
           "read-only bridge rejects shell verbs");

    const AutomationStatusSnapshot status{
        .position = "Ln 4, Col 9", .encoding = "UTF-8", .lineEnding = "LF",
        .format = "Plain Text", .mode = "Editor", .extra = ""};
    const auto parsedStatus = parseStatus(serializeStatus(status));
    expect(parsedStatus.has_value() && *parsedStatus == status,
           "status snapshot round-trips");

    const AutomationWindowSnapshot window{
        .pid = 1234U,
        .nativeHandle = 0x1234U,
        .outerBounds = {10, 20, 1200, 800},
        .clientBounds = {12, 44, 1196, 754},
        .dpi = 144U,
        .maximized = false,
        .executablePath = "C:\\Apps\\notepadFasaFiso_gui.exe",
    };
    const auto parsedWindow = parseWindow(serializeWindow(window));
    expect(parsedWindow.has_value() && *parsedWindow == window,
           "window snapshot round-trips");
}

void testScenarioParsing() {
    using namespace nff::gui::automation;

    const auto scenario = parseScenario(R"SCN(
# designer reproduction
click menu.view
wait dialog.settings visible
type "Türkçe deneme"
drag tab.2 pane.1 --duration-ms 220
snapshot "artifacts/settings before"
)SCN");
    expect(scenario.has_value() && scenario->commands.size() == 5U,
           "scenario parser keeps executable commands only");

    if (scenario && scenario->commands.size() == 5U) {
        expect(scenario->commands[0].kind == ScenarioCommandKind::Click,
               "click command kind parsed");
        expect(scenario->commands[2].arguments.size() == 1U &&
                   scenario->commands[2].arguments[0] == "Türkçe deneme",
               "quoted UTF-8 scenario argument preserved");
        expect(scenario->commands[3].durationMs == 220U,
               "drag duration option parsed");
    }
}

void testScenarioRejectsUnsafeOrAmbiguousLines() {
    using namespace nff::gui::automation;

    expect(!parseScenario("shell calc.exe\n").has_value(),
           "unknown scenario verbs are rejected");
    expect(!parseScenario("resize 0 0 -1 100\n").has_value(),
           "negative scenario dimensions are rejected");
    expect(!parseScenario("click bad/id\n").has_value(),
           "invalid semantic ids are rejected in scenarios");
    expect(!parseScenario("attach nope\n").has_value(),
           "attach requires numeric pid");
    expect(!parseScenario("drag tab.1\n").has_value(),
           "drag requires source and target");
    expect(!parseScenario("maximize trailing\n").has_value(),
           "fixed-arity commands reject trailing garbage");

    const auto hiddenWait = parseScenario("wait dialog.settings hidden\n");
    expect(hiddenWait.has_value(),
           "scenario wait supports hidden/absent synchronization for deterministic teardown");

    const AutomationElementSnapshot visibleDialog{
        .id = "dialog.settings", .role = "window", .bounds = {10, 10, 300, 200},
        .visible = true, .enabled = true, .checked = false, .text = {}};
    expect(elementMatchesWaitState(visibleDialog, "visible"),
           "visible wait matches a visible element");
    expect(!elementMatchesWaitState(visibleDialog, "hidden"),
           "hidden wait does not match a visible element");
    expect(elementMatchesWaitState(std::nullopt, "hidden"),
           "hidden wait treats a destroyed element as synchronized");
}

}

int main() {
    testProtocolRoundTrip();
    testProtocolRejectsMalformedInput();
    testGeometry();
    testSemanticIdAndExecutableValidation();
    testRequiredStaticIds();
    testSemanticTextEnrichment();
    testWindowsCommandLineQuoting();
    testAutomationStateRootArgumentExtraction();
    testAutomationStateRootLaunchArgumentInjection();
    testArtifactRelativePathValidation();
    testBridgeCommandProtocol();
    testScenarioParsing();
    testScenarioRejectsUnsafeOrAmbiguousLines();

    if (failures != 0) {
        std::cerr << failures << " GUI automation test(s) failed\n";
        return 1;
    }

    return 0;
}
