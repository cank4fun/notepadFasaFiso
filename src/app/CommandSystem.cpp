#include "notepadFasaFiso/app/CommandSystem.hpp"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace nff::app {
namespace {

constexpr std::array descriptors{
    CommandDescriptor{CommandId::NewDocument, "file.new", CommandScope::Application, false},
    CommandDescriptor{CommandId::OpenFile, "file.open", CommandScope::Application, false},
    CommandDescriptor{CommandId::Settings, "app.settings", CommandScope::Application, false},
    CommandDescriptor{CommandId::Save, "file.save", CommandScope::Document, false},
    CommandDescriptor{CommandId::SaveAs, "file.save-as", CommandScope::Document, false},
    CommandDescriptor{CommandId::SaveCopy, "file.save-copy", CommandScope::Document, false},
    CommandDescriptor{CommandId::Reload, "file.reload", CommandScope::Document, false},
    CommandDescriptor{CommandId::ReopenAsUtf8, "file.reopen-as.utf8", CommandScope::Document, false},
    CommandDescriptor{CommandId::ReopenAsUtf16LE, "file.reopen-as.utf16le", CommandScope::Document, false},
    CommandDescriptor{CommandId::ReopenAsUtf16BE, "file.reopen-as.utf16be", CommandScope::Document, false},
    CommandDescriptor{CommandId::ReopenAsUtf32LE, "file.reopen-as.utf32le", CommandScope::Document, false},
    CommandDescriptor{CommandId::ReopenAsUtf32BE, "file.reopen-as.utf32be", CommandScope::Document, false},
    CommandDescriptor{CommandId::ReopenAsWindows1252, "file.reopen-as.windows1252", CommandScope::Document, false},
    CommandDescriptor{CommandId::ReopenAsWindows1254, "file.reopen-as.windows1254", CommandScope::Document, false},
    CommandDescriptor{CommandId::CloseView, "view.close", CommandScope::View, false},
    CommandDescriptor{CommandId::ClosePane, "pane.close", CommandScope::Workspace, false},
    CommandDescriptor{CommandId::Undo, "edit.undo", CommandScope::Document, false},
    CommandDescriptor{CommandId::Redo, "edit.redo", CommandScope::Document, false},
    CommandDescriptor{CommandId::Cut, "edit.cut", CommandScope::Document, false},
    CommandDescriptor{CommandId::Copy, "edit.copy", CommandScope::Document, false},
    CommandDescriptor{CommandId::Paste, "edit.paste", CommandScope::Document, false},
    CommandDescriptor{CommandId::SelectAll, "edit.select-all", CommandScope::Document, false},
    CommandDescriptor{CommandId::Find, "search.find", CommandScope::Document, false},
    CommandDescriptor{CommandId::FindNext, "search.find-next", CommandScope::Document, false},
    CommandDescriptor{CommandId::FindPrevious, "search.find-previous", CommandScope::Document, false},
    CommandDescriptor{CommandId::Replace, "search.replace", CommandScope::Document, false},
    CommandDescriptor{CommandId::GoToLine, "search.goto-line", CommandScope::Document, false},
    CommandDescriptor{CommandId::SplitHorizontal, "pane.split-horizontal", CommandScope::View, false},
    CommandDescriptor{CommandId::SplitVertical, "pane.split-vertical", CommandScope::View, false},
    CommandDescriptor{CommandId::ToggleSidebar, "view.sidebar", CommandScope::Application, true},
    CommandDescriptor{CommandId::ToggleTabs, "view.tabs", CommandScope::Application, true},
    CommandDescriptor{CommandId::ToggleWordWrap, "view.word-wrap", CommandScope::View, true},
    CommandDescriptor{CommandId::ToggleLineNumbers, "view.line-numbers", CommandScope::View, true},
    CommandDescriptor{CommandId::SwitchToEditor, "mode.editor", CommandScope::Document, true},
    CommandDescriptor{CommandId::SwitchToViewer, "mode.viewer", CommandScope::Document, true},
    CommandDescriptor{CommandId::SwitchToHex, "mode.hex", CommandScope::Document, true},
    CommandDescriptor{CommandId::ToggleFollow, "viewer.follow", CommandScope::View, true},
    CommandDescriptor{CommandId::ViewerPerformanceAutomatic, "viewer.performance.auto", CommandScope::View, true},
    CommandDescriptor{CommandId::ViewerPerformanceFast, "viewer.performance.fast", CommandScope::View, true},
    CommandDescriptor{CommandId::ViewerPerformanceMemorySaver, "viewer.performance.memory-saver", CommandScope::View, true},
    CommandDescriptor{CommandId::EncodingUtf8, "document.encoding.utf8", CommandScope::Document, true},
    CommandDescriptor{CommandId::EncodingUtf16LE, "document.encoding.utf16le", CommandScope::Document, true},
    CommandDescriptor{CommandId::EncodingUtf16BE, "document.encoding.utf16be", CommandScope::Document, true},
    CommandDescriptor{CommandId::EncodingUtf32LE, "document.encoding.utf32le", CommandScope::Document, true},
    CommandDescriptor{CommandId::EncodingUtf32BE, "document.encoding.utf32be", CommandScope::Document, true},
    CommandDescriptor{CommandId::EncodingWindows1252, "document.encoding.windows1252", CommandScope::Document, true},
    CommandDescriptor{CommandId::EncodingWindows1254, "document.encoding.windows1254", CommandScope::Document, true},
    CommandDescriptor{CommandId::ToggleBom, "document.bom", CommandScope::Document, true},
    CommandDescriptor{CommandId::LineEndingLF, "document.line-ending.lf", CommandScope::Document, true},
    CommandDescriptor{CommandId::LineEndingCRLF, "document.line-ending.crlf", CommandScope::Document, true},
    CommandDescriptor{CommandId::LineEndingCR, "document.line-ending.cr", CommandScope::Document, true},
    CommandDescriptor{CommandId::FormatValidate, "format.validate", CommandScope::Document, false},
    CommandDescriptor{CommandId::FormatPretty, "format.pretty", CommandScope::Document, false},
    CommandDescriptor{CommandId::FormatMinify, "format.minify", CommandScope::Document, false},
    CommandDescriptor{CommandId::ConvertExport, "file.convert-export", CommandScope::Document, false},
    CommandDescriptor{CommandId::TextTrimTrailingWhitespace, "text.trim-trailing-whitespace",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::TextRemoveEmptyLines, "text.remove-empty-lines",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::TextRemoveDuplicateLines, "text.remove-duplicate-lines",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::TextSortAscending, "text.sort-ascending",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::TextSortDescending, "text.sort-descending",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::TextReverseLines, "text.reverse-lines",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::TextTabsToSpaces, "text.tabs-to-spaces",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::TextSpacesToTabs, "text.spaces-to-tabs",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::TextLowercaseAscii, "text.lowercase-ascii",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::TextUppercaseAscii, "text.uppercase-ascii",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::SidebarChooseFolder, "sidebar.choose-folder",
                      CommandScope::Workspace, false},
    CommandDescriptor{CommandId::SidebarReindexFolder, "sidebar.reindex-folder",
                      CommandScope::Workspace, false},
    CommandDescriptor{CommandId::SidebarClearFolder, "sidebar.clear-folder",
                      CommandScope::Workspace, false},
    CommandDescriptor{CommandId::TextColorChoose, "text.color.choose",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::TextColorClear, "text.color.clear",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::SelectionFontFamilyChoose, "selection.font-family.choose",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::SelectionFontFamilyClear, "selection.font-family.clear",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::SelectionFontSizeChoose, "selection.font-size.choose",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::SelectionFontSizeClear, "selection.font-size.clear",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::SelectionSpoilerSet, "selection.spoiler.set",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::SelectionSpoilerClear, "selection.spoiler.clear",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::SelectionAppearanceReset, "selection.appearance.reset",
                      CommandScope::Document, false},
    CommandDescriptor{CommandId::ToggleLinkDetection, "view.links",
                      CommandScope::Application, true},
};

[[nodiscard]] bool viewerMode(const CommandContext& context) noexcept {
    return context.hasDocument && context.openMode == core::OpenMode::Viewer;
}

}

std::span<const CommandDescriptor> CommandCatalog::commands() noexcept {
    return descriptors;
}

const CommandDescriptor* CommandCatalog::find(const CommandId id) noexcept {
    const auto iterator = std::find_if(descriptors.begin(), descriptors.end(), [id](const auto& item) {
        return item.id == id;
    });
    return iterator == descriptors.end() ? nullptr : &*iterator;
}

std::optional<CommandId> CommandCatalog::findByName(const std::string_view name) noexcept {
    const auto iterator = std::find_if(descriptors.begin(), descriptors.end(), [name](const auto& item) {
        return item.name == name;
    });
    if (iterator == descriptors.end()) {
        return std::nullopt;
    }
    return iterator->id;
}

CommandState CommandCatalog::state(const CommandId id, const CommandContext& context) noexcept {
    CommandState result;
    switch (id) {
    case CommandId::NewDocument:
    case CommandId::OpenFile:
    case CommandId::Settings:
    case CommandId::SidebarChooseFolder:
        result.enabled = true;
        break;
    case CommandId::Save:
        result.enabled = context.hasDocument && context.editable && context.textBufferLoaded &&
                         context.modified;
        break;
    case CommandId::SaveAs:
        result.enabled = context.hasDocument && context.editable && context.textBufferLoaded;
        break;
    case CommandId::SaveCopy:
    case CommandId::ConvertExport:
        result.enabled = context.hasDocument && context.textBufferLoaded;
        break;
    case CommandId::Reload:
        result.enabled = context.hasDocument && context.hasBackingFile;
        break;
    case CommandId::ReopenAsUtf8:
    case CommandId::ReopenAsUtf16LE:
    case CommandId::ReopenAsUtf16BE:
    case CommandId::ReopenAsUtf32LE:
    case CommandId::ReopenAsUtf32BE:
    case CommandId::ReopenAsWindows1252:
    case CommandId::ReopenAsWindows1254:
        result.enabled = context.hasDocument && context.hasBackingFile && !context.modified &&
                         !context.textBufferLoaded &&
                         (context.openMode == core::OpenMode::BinaryPreview ||
                          context.openMode == core::OpenMode::Viewer);
        break;
    case CommandId::CloseView:
        result.enabled = context.hasView;
        break;
    case CommandId::ClosePane:
        result.enabled = context.paneCount > 1U;
        break;
    case CommandId::Undo:
        result.enabled = context.editable && context.canUndo;
        break;
    case CommandId::Redo:
        result.enabled = context.editable && context.canRedo;
        break;
    case CommandId::Cut:
        result.enabled = context.editable && context.hasSelection;
        break;
    case CommandId::Copy:
        result.enabled = context.hasDocument && context.hasSelection;
        break;
    case CommandId::Paste:
        result.enabled = context.editable && context.canPaste;
        break;
    case CommandId::SelectAll:
    case CommandId::GoToLine:
        result.enabled = context.hasDocument;
        break;
    case CommandId::Find:
    case CommandId::FindNext:
    case CommandId::FindPrevious:
        result.enabled = context.hasDocument &&
                         context.openMode != core::OpenMode::BinaryPreview;
        break;
    case CommandId::Replace:
        result.enabled = context.hasDocument && context.editable && context.textBufferLoaded &&
                         context.openMode == core::OpenMode::Editor;
        break;
    case CommandId::SplitHorizontal:
    case CommandId::SplitVertical:
        result.enabled = context.hasView;
        break;
    case CommandId::ToggleSidebar:
        result.enabled = true;
        result.checked = context.sidebarVisible;
        break;
    case CommandId::ToggleTabs:
        result.enabled = true;
        result.checked = context.tabsEnabled;
        break;
    case CommandId::ToggleWordWrap:
        result.enabled = context.hasView;
        result.checked = context.wordWrap;
        break;
    case CommandId::ToggleLineNumbers:
        result.enabled = context.hasView;
        result.checked = context.lineNumbersVisible;
        break;
    case CommandId::SwitchToEditor:
        result.enabled = context.hasDocument && context.hasBackingFile &&
                         (context.openMode != core::OpenMode::BinaryPreview ||
                          context.textBufferLoaded);
        result.checked = context.openMode == core::OpenMode::Editor;
        break;
    case CommandId::SwitchToViewer:
        result.enabled = context.hasDocument && context.hasBackingFile &&
                         (context.openMode != core::OpenMode::BinaryPreview ||
                          context.textBufferLoaded);
        result.checked = context.openMode == core::OpenMode::Viewer;
        break;
    case CommandId::SwitchToHex:
        result.enabled = context.hasDocument && context.hasBackingFile && !context.modified;
        result.checked = context.openMode == core::OpenMode::BinaryPreview;
        break;
    case CommandId::ToggleFollow:
        result.enabled = viewerMode(context) && context.followAvailable;
        result.checked = context.followEnabled;
        break;
    case CommandId::ViewerPerformanceAutomatic:
        result.enabled = viewerMode(context);
        result.checked = context.viewerPerformance == viewer::PerformanceProfile::Automatic;
        break;
    case CommandId::ViewerPerformanceFast:
        result.enabled = viewerMode(context);
        result.checked = context.viewerPerformance == viewer::PerformanceProfile::Fast;
        break;
    case CommandId::ViewerPerformanceMemorySaver:
        result.enabled = viewerMode(context);
        result.checked = context.viewerPerformance == viewer::PerformanceProfile::MemorySaver;
        break;
    case CommandId::EncodingUtf8:
        result.enabled = context.editable && context.textBufferLoaded;
        result.checked = context.saveEncoding == encoding::Encoding::Utf8;
        break;
    case CommandId::EncodingUtf16LE:
        result.enabled = context.editable && context.textBufferLoaded;
        result.checked = context.saveEncoding == encoding::Encoding::Utf16LE;
        break;
    case CommandId::EncodingUtf16BE:
        result.enabled = context.editable && context.textBufferLoaded;
        result.checked = context.saveEncoding == encoding::Encoding::Utf16BE;
        break;
    case CommandId::EncodingUtf32LE:
        result.enabled = context.editable && context.textBufferLoaded;
        result.checked = context.saveEncoding == encoding::Encoding::Utf32LE;
        break;
    case CommandId::EncodingUtf32BE:
        result.enabled = context.editable && context.textBufferLoaded;
        result.checked = context.saveEncoding == encoding::Encoding::Utf32BE;
        break;
    case CommandId::EncodingWindows1252:
        result.enabled = context.editable && context.textBufferLoaded;
        result.checked = context.saveEncoding == encoding::Encoding::Windows1252;
        break;
    case CommandId::EncodingWindows1254:
        result.enabled = context.editable && context.textBufferLoaded;
        result.checked = context.saveEncoding == encoding::Encoding::Windows1254;
        break;
    case CommandId::ToggleBom:
        result.enabled = context.editable && context.textBufferLoaded &&
                         (context.saveEncoding == encoding::Encoding::Utf8 ||
                          context.saveEncoding == encoding::Encoding::Utf16LE ||
                          context.saveEncoding == encoding::Encoding::Utf16BE ||
                          context.saveEncoding == encoding::Encoding::Utf32LE ||
                          context.saveEncoding == encoding::Encoding::Utf32BE);
        result.checked = context.writesBom;
        break;
    case CommandId::LineEndingLF:
        result.enabled = context.editable && context.textBufferLoaded;
        result.checked = context.lineEnding == core::LineEnding::LF;
        break;
    case CommandId::LineEndingCRLF:
        result.enabled = context.editable && context.textBufferLoaded;
        result.checked = context.lineEnding == core::LineEnding::CRLF;
        break;
    case CommandId::LineEndingCR:
        result.enabled = context.editable && context.textBufferLoaded;
        result.checked = context.lineEnding == core::LineEnding::CR;
        break;
    case CommandId::FormatValidate:
        result.enabled = context.hasDocument && context.textBufferLoaded && context.formatCanValidate;
        break;
    case CommandId::FormatPretty:
        result.enabled = context.hasDocument && context.editable && context.textBufferLoaded &&
                         context.formatCanPrettyPrint;
        break;
    case CommandId::FormatMinify:
        result.enabled = context.hasDocument && context.editable && context.textBufferLoaded &&
                         context.formatCanMinify;
        break;
    case CommandId::TextTrimTrailingWhitespace:
    case CommandId::TextRemoveEmptyLines:
    case CommandId::TextRemoveDuplicateLines:
    case CommandId::TextSortAscending:
    case CommandId::TextSortDescending:
    case CommandId::TextReverseLines:
    case CommandId::TextTabsToSpaces:
    case CommandId::TextSpacesToTabs:
    case CommandId::TextLowercaseAscii:
    case CommandId::TextUppercaseAscii:
        result.enabled = context.hasDocument && context.editable && context.textBufferLoaded &&
                         context.openMode == core::OpenMode::Editor;
        break;
    case CommandId::SidebarReindexFolder:
    case CommandId::SidebarClearFolder:
        result.enabled = context.sidebarFolderActive;
        break;
    case CommandId::TextColorChoose:
    case CommandId::TextColorClear:
    case CommandId::SelectionFontFamilyChoose:
    case CommandId::SelectionFontFamilyClear:
    case CommandId::SelectionFontSizeChoose:
    case CommandId::SelectionFontSizeClear:
    case CommandId::SelectionSpoilerSet:
    case CommandId::SelectionSpoilerClear:
    case CommandId::SelectionAppearanceReset:
        result.enabled = context.hasDocument && context.editable && context.textBufferLoaded &&
                         context.openMode == core::OpenMode::Editor && context.hasSelection;
        break;
    case CommandId::ToggleLinkDetection:
        result.enabled = true;
        result.checked = context.linkDetectionEnabled;
        break;
    }
    return result;
}

bool CommandDispatcher::bind(const CommandId id, Handler handler) {
    if (!handler || CommandCatalog::find(id) == nullptr || hasHandler(id)) {
        return false;
    }
    bindings_.push_back({id, std::move(handler)});
    return true;
}

bool CommandDispatcher::unbind(const CommandId id) noexcept {
    const auto iterator = std::find_if(bindings_.begin(), bindings_.end(), [id](const Binding& item) {
        return item.id == id;
    });
    if (iterator == bindings_.end()) {
        return false;
    }
    bindings_.erase(iterator);
    return true;
}

bool CommandDispatcher::hasHandler(const CommandId id) const noexcept {
    return std::any_of(bindings_.begin(), bindings_.end(), [id](const Binding& item) {
        return item.id == id;
    });
}

CommandDispatchResult CommandDispatcher::dispatch(const CommandId id,
                                                   const CommandContext& context) const {
    const auto state = CommandCatalog::state(id, context);
    if (!state.enabled) {
        return {false, false, std::make_error_code(std::errc::operation_not_permitted)};
    }

    const auto iterator = std::find_if(bindings_.begin(), bindings_.end(), [id](const Binding& item) {
        return item.id == id;
    });
    if (iterator == bindings_.end()) {
        return {false, false, std::make_error_code(std::errc::function_not_supported)};
    }

    const auto error = iterator->handler();
    return {true, !error, error};
}

}
