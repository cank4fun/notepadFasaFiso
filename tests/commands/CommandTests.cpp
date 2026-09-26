#include "notepadFasaFiso/app/CommandSystem.hpp"

#include <iostream>
#include <system_error>

namespace {

int failures = 0;

void expect(const bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void testCatalogAndStateEvaluation() {
    const auto commands = nff::app::CommandCatalog::commands();
    expect(!commands.empty(), "command catalog is populated");
    const auto saveId = nff::app::CommandCatalog::findByName("file.save");
    expect(saveId == nff::app::CommandId::Save, "stable command name resolves");
    expect(!nff::app::CommandCatalog::findByName("missing.command"),
           "unknown command name does not resolve");

    nff::app::CommandContext context;
    auto state = nff::app::CommandCatalog::state(nff::app::CommandId::Save, context);
    expect(!state.enabled, "save disabled without document");
    expect(nff::app::CommandCatalog::state(
               nff::app::CommandId::SidebarChooseFolder, context).enabled,
           "search folder chooser is always available");
    auto linkState = nff::app::CommandCatalog::state(
        nff::app::CommandId::ToggleLinkDetection, context);
    expect(linkState.enabled && !linkState.checked,
           "link detection toggle is available and reflects disabled state");
    context.linkDetectionEnabled = true;
    linkState = nff::app::CommandCatalog::state(
        nff::app::CommandId::ToggleLinkDetection, context);
    expect(linkState.enabled && linkState.checked,
           "link detection toggle reflects enabled state");

    expect(!nff::app::CommandCatalog::state(
               nff::app::CommandId::SidebarReindexFolder, context).enabled &&
               !nff::app::CommandCatalog::state(
                   nff::app::CommandId::SidebarClearFolder, context).enabled,
           "search folder maintenance is disabled without a chosen folder");
    context.sidebarFolderActive = true;
    expect(nff::app::CommandCatalog::state(
               nff::app::CommandId::SidebarReindexFolder, context).enabled &&
               nff::app::CommandCatalog::state(
                   nff::app::CommandId::SidebarClearFolder, context).enabled,
           "chosen search folder enables reindex and clear commands");

    context.hasDocument = true;
    context.hasView = true;
    context.hasBackingFile = true;
    context.editable = true;
    context.modified = true;
    context.textBufferLoaded = true;
    context.hasSelection = true;
    context.canUndo = true;
    context.canPaste = true;
    context.paneCount = 2U;
    state = nff::app::CommandCatalog::state(nff::app::CommandId::Save, context);
    expect(state.enabled, "save enabled for modified editable document");
    expect(nff::app::CommandCatalog::state(nff::app::CommandId::Cut, context).enabled,
           "cut enabled for editable selection");
    expect(nff::app::CommandCatalog::state(nff::app::CommandId::ClosePane, context).enabled,
           "close pane enabled with multiple panes");
    context.hasView = false;
    expect(nff::app::CommandCatalog::state(nff::app::CommandId::ClosePane, context).enabled,
           "empty active pane can still be closed when another pane exists");
    context.hasView = true;
    expect(nff::app::CommandCatalog::state(nff::app::CommandId::Find, context).enabled &&
               nff::app::CommandCatalog::state(nff::app::CommandId::FindNext, context).enabled &&
               nff::app::CommandCatalog::state(nff::app::CommandId::FindPrevious, context).enabled,
           "editor exposes find navigation commands");
    expect(nff::app::CommandCatalog::state(nff::app::CommandId::Replace, context).enabled,
           "editor exposes replace command");

    context.formatCanValidate = true;
    context.formatCanPrettyPrint = true;
    context.formatCanMinify = true;
    expect(nff::app::CommandCatalog::state(nff::app::CommandId::FormatValidate, context).enabled &&
               nff::app::CommandCatalog::state(nff::app::CommandId::FormatPretty, context).enabled &&
               nff::app::CommandCatalog::state(nff::app::CommandId::FormatMinify, context).enabled,
           "loaded editable JSON exposes format commands");
    expect(nff::app::CommandCatalog::state(nff::app::CommandId::ConvertExport, context).enabled,
           "loaded text exposes convert/export command");
    expect(nff::app::CommandCatalog::state(
               nff::app::CommandId::TextTrimTrailingWhitespace, context).enabled &&
               nff::app::CommandCatalog::state(
                   nff::app::CommandId::TextRemoveDuplicateLines, context).enabled &&
               nff::app::CommandCatalog::state(
                   nff::app::CommandId::TextSortAscending, context).enabled &&
               nff::app::CommandCatalog::state(
                   nff::app::CommandId::TextTabsToSpaces, context).enabled &&
               nff::app::CommandCatalog::state(
                   nff::app::CommandId::TextUppercaseAscii, context).enabled,
           "editable loaded text exposes text utility commands");
    expect(nff::app::CommandCatalog::state(nff::app::CommandId::TextColorChoose, context).enabled &&
               nff::app::CommandCatalog::state(nff::app::CommandId::TextColorClear, context).enabled &&
               nff::app::CommandCatalog::state(nff::app::CommandId::SelectionFontFamilyChoose, context).enabled &&
               nff::app::CommandCatalog::state(nff::app::CommandId::SelectionFontFamilyClear, context).enabled &&
               nff::app::CommandCatalog::state(nff::app::CommandId::SelectionFontSizeChoose, context).enabled &&
               nff::app::CommandCatalog::state(nff::app::CommandId::SelectionFontSizeClear, context).enabled &&
               nff::app::CommandCatalog::state(nff::app::CommandId::SelectionSpoilerSet, context).enabled &&
               nff::app::CommandCatalog::state(nff::app::CommandId::SelectionSpoilerClear, context).enabled &&
               nff::app::CommandCatalog::state(nff::app::CommandId::SelectionAppearanceReset, context).enabled,
           "editable selection exposes the complete selection appearance command family");

    context.saveEncoding = nff::encoding::Encoding::Utf8;
    context.writesBom = false;
    context.lineEnding = nff::core::LineEnding::LF;
    auto encodingState =
        nff::app::CommandCatalog::state(nff::app::CommandId::EncodingUtf8, context);
    expect(encodingState.enabled && encodingState.checked,
           "editor exposes checked output encoding command");
    auto bomState = nff::app::CommandCatalog::state(nff::app::CommandId::ToggleBom, context);
    expect(bomState.enabled && !bomState.checked, "UTF-8 exposes optional BOM command");
    auto eolState = nff::app::CommandCatalog::state(nff::app::CommandId::LineEndingLF, context);
    expect(eolState.enabled && eolState.checked, "current line ending command is checked");

    context.saveEncoding = nff::encoding::Encoding::Windows1254;
    bomState = nff::app::CommandCatalog::state(nff::app::CommandId::ToggleBom, context);
    expect(!bomState.enabled, "legacy encoding disables BOM command");

    context.hasSelection = false;
    expect(!nff::app::CommandCatalog::state(nff::app::CommandId::TextColorChoose, context).enabled &&
               !nff::app::CommandCatalog::state(nff::app::CommandId::SelectionFontFamilyChoose, context).enabled &&
               !nff::app::CommandCatalog::state(nff::app::CommandId::SelectionSpoilerSet, context).enabled &&
               !nff::app::CommandCatalog::state(nff::app::CommandId::SelectionAppearanceReset, context).enabled,
           "selection appearance commands require a selection");
    context.hasSelection = true;

    context.openMode = nff::core::OpenMode::Viewer;
    context.editable = false;
    expect(nff::app::CommandCatalog::state(nff::app::CommandId::FormatValidate, context).enabled,
           "loaded read-only text still allows JSON validation");
    expect(!nff::app::CommandCatalog::state(nff::app::CommandId::FormatPretty, context).enabled &&
               !nff::app::CommandCatalog::state(nff::app::CommandId::FormatMinify, context).enabled,
           "read-only view disables in-place JSON transforms");
    expect(nff::app::CommandCatalog::state(nff::app::CommandId::SwitchToEditor, context).enabled,
           "file-backed viewer exposes edit-anyway command");
    expect(nff::app::CommandCatalog::state(nff::app::CommandId::SwitchToViewer, context).checked,
           "viewer mode command exposes checked state");
    expect(nff::app::CommandCatalog::state(nff::app::CommandId::Find, context).enabled &&
               nff::app::CommandCatalog::state(nff::app::CommandId::FindNext, context).enabled &&
               nff::app::CommandCatalog::state(nff::app::CommandId::FindPrevious, context).enabled,
           "scalable viewer keeps find navigation available");
    expect(!nff::app::CommandCatalog::state(nff::app::CommandId::Replace, context).enabled,
           "scalable viewer keeps replace disabled");
    expect(!nff::app::CommandCatalog::state(
               nff::app::CommandId::TextTrimTrailingWhitespace, context).enabled &&
               !nff::app::CommandCatalog::state(
                   nff::app::CommandId::TextSortAscending, context).enabled &&
               !nff::app::CommandCatalog::state(
                   nff::app::CommandId::TextUppercaseAscii, context).enabled,
           "scalable viewer keeps in-place text utilities disabled");
    expect(!nff::app::CommandCatalog::state(nff::app::CommandId::TextColorChoose, context).enabled &&
               !nff::app::CommandCatalog::state(nff::app::CommandId::TextColorClear, context).enabled,
           "scalable viewer keeps text color metadata commands disabled");
    expect(!nff::app::CommandCatalog::state(nff::app::CommandId::EncodingUtf8, context).enabled &&
               !nff::app::CommandCatalog::state(nff::app::CommandId::LineEndingLF, context).enabled,
           "scalable viewer keeps representation conversion disabled until Edit Anyway");
    context.followAvailable = true;
    context.followEnabled = true;
    context.viewerPerformance = nff::viewer::PerformanceProfile::Fast;
    state = nff::app::CommandCatalog::state(nff::app::CommandId::ToggleFollow, context);
    expect(state.enabled && state.checked, "follow command exposes checked viewer state");
    state = nff::app::CommandCatalog::state(nff::app::CommandId::ViewerPerformanceFast, context);
    expect(state.enabled && state.checked, "viewer performance command exposes radio state");

    context.modified = false;
    context.textBufferLoaded = false;
    expect(nff::app::CommandCatalog::state(
               nff::app::CommandId::ReopenAsWindows1254, context).enabled,
           "scalable viewer can change an explicit source encoding without materializing text");

    context.openMode = nff::core::OpenMode::BinaryPreview;
    context.modified = false;
    context.textBufferLoaded = false;
    expect(nff::app::CommandCatalog::state(
               nff::app::CommandId::ReopenAsWindows1254, context).enabled,
           "hex preview exposes explicit source encoding reopen");
    context.modified = true;
    expect(!nff::app::CommandCatalog::state(
               nff::app::CommandId::ReopenAsWindows1254, context).enabled,
           "explicit source encoding reopen never discards modified text");
    context.modified = false;
    context.textBufferLoaded = true;
    expect(!nff::app::CommandCatalog::state(
               nff::app::CommandId::ReopenAsWindows1254, context).enabled,
           "explicit source encoding reopen stays disabled for materialized text");
    context.textBufferLoaded = false;
    expect(!nff::app::CommandCatalog::state(nff::app::CommandId::Find, context).enabled &&
               !nff::app::CommandCatalog::state(nff::app::CommandId::FindNext, context).enabled &&
               !nff::app::CommandCatalog::state(nff::app::CommandId::FindPrevious, context).enabled,
           "binary preview disables text search commands");
    expect(!nff::app::CommandCatalog::state(nff::app::CommandId::Replace, context).enabled,
           "binary preview disables replace command");
}

void testCriticalSettingsCommandIsExposed() {
    const auto id = nff::app::CommandCatalog::findByName("app.settings");
    expect(id.has_value(), "critical settings command is present in the application catalog");
    if (id) {
        const auto descriptor = nff::app::CommandCatalog::find(*id);
        expect(descriptor != nullptr && descriptor->scope == nff::app::CommandScope::Application,
               "settings command is application scoped");
        expect(nff::app::CommandCatalog::state(*id, {}).enabled,
               "settings command is always available");
    }
}

void testHexPreviewModeCommandIsExposed() {
    const auto id = nff::app::CommandCatalog::findByName("mode.hex");
    expect(id.has_value(), "Hex Preview mode command is present");
    if (!id) {
        return;
    }

    nff::app::CommandContext context;
    context.hasDocument = true;
    context.hasView = true;
    context.hasBackingFile = true;
    context.textBufferLoaded = true;
    context.openMode = nff::core::OpenMode::Viewer;
    auto state = nff::app::CommandCatalog::state(*id, context);
    expect(state.enabled && !state.checked,
           "file-backed text view can switch to Hex Preview");

    context.openMode = nff::core::OpenMode::BinaryPreview;
    state = nff::app::CommandCatalog::state(*id, context);
    expect(state.enabled && state.checked, "Hex Preview command exposes checked state");

    context.modified = true;
    state = nff::app::CommandCatalog::state(*id, context);
    expect(!state.enabled, "Hex Preview switch refuses to hide unsaved edits");
}

void testDispatcherHonorsStateAndBindings() {
    nff::app::CommandDispatcher dispatcher;
    int calls = 0;
    expect(dispatcher.bind(nff::app::CommandId::Save, [&calls] {
        ++calls;
        return std::error_code{};
    }), "bind save handler");
    expect(!dispatcher.bind(nff::app::CommandId::Save, [] { return std::error_code{}; }),
           "duplicate command binding rejected");

    nff::app::CommandContext disabled;
    auto result = dispatcher.dispatch(nff::app::CommandId::Save, disabled);
    expect(!result.executed && calls == 0, "disabled command never invokes handler");

    nff::app::CommandContext enabled;
    enabled.hasDocument = true;
    enabled.editable = true;
    enabled.modified = true;
    enabled.textBufferLoaded = true;
    result = dispatcher.dispatch(nff::app::CommandId::Save, enabled);
    expect(static_cast<bool>(result) && calls == 1, "enabled command invokes handler once");

    expect(dispatcher.unbind(nff::app::CommandId::Save), "unbind registered command");
    expect(!dispatcher.hasHandler(nff::app::CommandId::Save), "handler removed");
    result = dispatcher.dispatch(nff::app::CommandId::Save, enabled);
    expect(!result.handled && !result.executed, "unbound command reports unhandled");
}

}

int main() {
    testCatalogAndStateEvaluation();
    testCriticalSettingsCommandIsExposed();
    testHexPreviewModeCommandIsExposed();
    testDispatcherHonorsStateAndBindings();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "all command tests passed\n";
    return 0;
}
