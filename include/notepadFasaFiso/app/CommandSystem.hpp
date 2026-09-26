#pragma once

#include "notepadFasaFiso/core/FileSniffer.hpp"
#include "notepadFasaFiso/encoding/EncodingDetector.hpp"
#include "notepadFasaFiso/viewer/ViewCache.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace nff::app {

enum class CommandId : std::uint16_t {
    NewDocument,
    OpenFile,
    Settings,
    Save,
    SaveAs,
    SaveCopy,
    Reload,
    ReopenAsUtf8,
    ReopenAsUtf16LE,
    ReopenAsUtf16BE,
    ReopenAsUtf32LE,
    ReopenAsUtf32BE,
    ReopenAsWindows1252,
    ReopenAsWindows1254,
    CloseView,
    ClosePane,
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    SelectAll,
    Find,
    FindNext,
    FindPrevious,
    Replace,
    GoToLine,
    SplitHorizontal,
    SplitVertical,
    ToggleSidebar,
    ToggleTabs,
    ToggleWordWrap,
    ToggleLineNumbers,
    SwitchToEditor,
    SwitchToViewer,
    SwitchToHex,
    ToggleFollow,
    ViewerPerformanceAutomatic,
    ViewerPerformanceFast,
    ViewerPerformanceMemorySaver,
    EncodingUtf8,
    EncodingUtf16LE,
    EncodingUtf16BE,
    EncodingUtf32LE,
    EncodingUtf32BE,
    EncodingWindows1252,
    EncodingWindows1254,
    ToggleBom,
    LineEndingLF,
    LineEndingCRLF,
    LineEndingCR,
    FormatPretty,
    FormatMinify,
    ConvertExport,
    FormatValidate,
    TextTrimTrailingWhitespace,
    TextRemoveEmptyLines,
    TextRemoveDuplicateLines,
    TextSortAscending,
    TextSortDescending,
    TextReverseLines,
    TextTabsToSpaces,
    TextSpacesToTabs,
    TextLowercaseAscii,
    TextUppercaseAscii,
    SidebarChooseFolder,
    SidebarReindexFolder,
    SidebarClearFolder,
    TextColorChoose,
    TextColorClear,
    SelectionFontFamilyChoose,
    SelectionFontFamilyClear,
    SelectionFontSizeChoose,
    SelectionFontSizeClear,
    SelectionSpoilerSet,
    SelectionSpoilerClear,
    SelectionAppearanceReset,
    ToggleLinkDetection,
};

enum class CommandScope : std::uint8_t {
    Application,
    Workspace,
    Document,
    View,
};

struct CommandDescriptor final {
    CommandId id{CommandId::NewDocument};
    std::string_view name;
    CommandScope scope{CommandScope::Application};
    bool checkable{false};
};

struct CommandContext final {
    bool hasDocument{false};
    bool hasView{false};
    bool hasBackingFile{false};
    bool editable{false};
    bool modified{false};
    bool textBufferLoaded{false};
    bool hasSelection{false};
    bool canUndo{false};
    bool canRedo{false};
    bool canPaste{false};
    bool sidebarVisible{true};
    bool sidebarFolderActive{false};
    bool tabsEnabled{true};
    bool wordWrap{true};
    bool lineNumbersVisible{false};
    bool linkDetectionEnabled{false};
    bool followAvailable{false};
    bool followEnabled{false};
    bool formatCanValidate{false};
    bool formatCanPrettyPrint{false};
    bool formatCanMinify{false};
    encoding::Encoding saveEncoding{encoding::Encoding::Utf8};
    bool writesBom{false};
    core::LineEnding lineEnding{core::LineEnding::Unknown};
    std::size_t paneCount{1};
    core::OpenMode openMode{core::OpenMode::Editor};
    viewer::PerformanceProfile viewerPerformance{viewer::PerformanceProfile::Automatic};
};

struct CommandState final {
    bool enabled{false};
    bool checked{false};
};

class CommandCatalog final {
public:
    [[nodiscard]] static std::span<const CommandDescriptor> commands() noexcept;
    [[nodiscard]] static const CommandDescriptor* find(CommandId id) noexcept;
    [[nodiscard]] static std::optional<CommandId> findByName(std::string_view name) noexcept;
    [[nodiscard]] static CommandState state(CommandId id,
                                            const CommandContext& context) noexcept;
};

struct CommandDispatchResult final {
    bool handled{false};
    bool executed{false};
    std::error_code error{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return handled && executed && !error;
    }
};

class CommandDispatcher final {
public:
    using Handler = std::function<std::error_code()>;

    [[nodiscard]] bool bind(CommandId id, Handler handler);
    [[nodiscard]] bool unbind(CommandId id) noexcept;
    [[nodiscard]] bool hasHandler(CommandId id) const noexcept;
    [[nodiscard]] CommandDispatchResult dispatch(CommandId id,
                                                 const CommandContext& context) const;

private:
    struct Binding final {
        CommandId id{CommandId::NewDocument};
        Handler handler;
    };

    std::vector<Binding> bindings_;
};

}
