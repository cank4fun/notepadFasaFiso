#pragma once

#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/core/FileSniffer.hpp"
#include "notepadFasaFiso/gui/GuiGeometry.hpp"
#include "notepadFasaFiso/metadata/MetadataStore.hpp"
#include "notepadFasaFiso/search/TextSearch.hpp"
#include "notepadFasaFiso/viewer/ViewCache.hpp"
#include "notepadFasaFiso/workspace/WorkspaceModel.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace nff::gui {

struct EditorHostAppearance final {
    std::span<const metadata::TextAppearanceSpan> spans{};
    std::span<const std::string> fontFamilies{};
    std::uint64_t revision{0U};
    bool hasSpoilers{false};
};

struct EditorHostBinding final {
    workspace::ViewId view{};
    core::DocumentId document{};
    core::OpenMode mode{core::OpenMode::Editor};
    viewer::PerformanceProfile viewerPerformance{viewer::PerformanceProfile::Automatic};
    std::filesystem::path path{};
    const core::DocumentProfile* documentProfile{nullptr};
    const storage::FileState* documentFileState{nullptr};
    std::string_view text{};
    bool textBufferLoaded{true};
    std::string_view fontFamily{};
    double fontPointSize{12.0};
    std::uint64_t documentRevision{0U};
    std::size_t preferredByteOffset{0U};
    bool wordWrap{true};
    bool lineNumbersVisible{false};
    bool followEnabled{false};
    std::span<const metadata::TextAppearanceSpan> appearanceSpans{};
    std::span<const std::string> appearanceFontFamilies{};
    std::uint64_t appearanceRevision{0U};
    bool hasSpoilers{false};
};

struct EditorHostViewState final {
    std::size_t caretOffset{0U};
    std::size_t anchorOffset{0U};
    std::size_t firstVisibleLine{0U};
};

struct EditorHostPollResult final {
    bool changed{false};
    std::error_code error{};

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

struct EditorHostSearchRequest final {
    std::string_view pattern{};
    search::SearchOptions options{};
    search::SearchDirection direction{search::SearchDirection::Forward};
    std::uint64_t startOffset{0U};
};

struct EditorHostSearchResult final {
    std::optional<search::SearchMatch> match;
    std::uint64_t bytesScanned{0U};
    std::error_code error{};
    bool wrapped{false};
    bool cancelled{false};

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

struct EditorHostRuntime final {
    std::uint64_t caretLine{1U};
    std::uint64_t caretColumn{1U};
    std::uint64_t selectionBytes{0U};
    std::uint64_t contentBytes{0U};
    std::uint64_t windowByteStart{0U};
    std::uint64_t windowByteEnd{0U};
    std::size_t viewerCacheResidentBytes{0U};
    std::size_t caretOffset{0U};
    std::size_t anchorOffset{0U};
    std::size_t firstVisibleLine{0U};
    viewer::PerformanceProfile resolvedViewerPerformance{viewer::PerformanceProfile::Automatic};
    bool hasSelection{false};
    bool canUndo{false};
    bool canRedo{false};
    bool canPaste{false};
    bool followWaiting{false};
};

enum class EditorHostCommand : std::uint8_t {
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    SelectAll,
};

struct EditorTextEdit final {
    std::size_t offset{0U};
    std::size_t eraseBytes{0U};
    std::string_view insertedText{};
    std::uint64_t baseRevision{0U};
    metadata::TextColorEditPolicy colorPolicy{metadata::TextColorEditPolicy::AdjustRanges};
};

class IEditorHostEvents {
public:
    virtual ~IEditorHostEvents() = default;

    [[nodiscard]] virtual bool applyTextEdit(workspace::ViewId view,
                                             const EditorTextEdit& edit) = 0;
    [[nodiscard]] virtual bool restoreTextAppearanceRange(
        workspace::ViewId view,
        std::uint64_t begin,
        std::uint64_t end,
        std::span<const metadata::TextAppearanceSpan> spans) = 0;
};

class IEditorHost {
public:
    virtual ~IEditorHost() = default;

    virtual void bind(const EditorHostBinding& binding) = 0;
    virtual void restoreViewState(const EditorHostViewState& state) = 0;
    virtual void setBounds(Rect bounds) = 0;
    virtual void setVisible(bool visible) = 0;
    virtual void setFocused(bool focused) = 0;
    [[nodiscard]] virtual EditorHostRuntime runtime() const noexcept = 0;
    [[nodiscard]] virtual bool execute(EditorHostCommand command) {
        static_cast<void>(command);
        return false;
    }
    virtual void refreshAppearance() {}
    [[nodiscard]] virtual bool recordTextAppearanceUndo(
        std::uint64_t begin,
        std::uint64_t end,
        std::span<const metadata::TextAppearanceSpan> before,
        std::span<const metadata::TextAppearanceSpan> after) {
        static_cast<void>(begin);
        static_cast<void>(end);
        static_cast<void>(before);
        static_cast<void>(after);
        return false;
    }
    [[nodiscard]] virtual EditorHostPollResult pollLiveContent() { return {}; }
    [[nodiscard]] virtual EditorHostSearchResult findText(
        const EditorHostSearchRequest& request) {
        static_cast<void>(request);
        return {{}, 0U, std::make_error_code(std::errc::operation_not_supported), false, false};
    }
    [[nodiscard]] virtual bool revealTextMatch(search::SearchMatch match,
                                               std::string_view pattern) {
        static_cast<void>(match);
        static_cast<void>(pattern);
        return false;
    }
    [[nodiscard]] virtual bool replaceTextRange(
        search::SearchMatch match,
        std::string_view replacement,
        metadata::TextColorEditPolicy colorPolicy = metadata::TextColorEditPolicy::AdjustRanges) {
        static_cast<void>(match);
        static_cast<void>(replacement);
        static_cast<void>(colorPolicy);
        return false;
    }
    [[nodiscard]] virtual bool replaceAllText(
        std::string_view text,
        metadata::TextColorEditPolicy colorPolicy = metadata::TextColorEditPolicy::AdjustRanges) {
        static_cast<void>(text);
        static_cast<void>(colorPolicy);
        return false;
    }
    [[nodiscard]] virtual std::error_code goToLine(std::uint64_t oneBasedLine) {
        static_cast<void>(oneBasedLine);
        return std::make_error_code(std::errc::operation_not_supported);
    }
    [[nodiscard]] virtual std::error_code goToPosition(std::uint64_t oneBasedLine,
                                                       std::uint64_t oneBasedColumn) {
        if (oneBasedColumn <= 1U) {
            return goToLine(oneBasedLine);
        }
        static_cast<void>(oneBasedLine);
        return std::make_error_code(std::errc::operation_not_supported);
    }
};

class IEditorHostFactory {
public:
    virtual ~IEditorHostFactory() = default;

    [[nodiscard]] virtual std::unique_ptr<IEditorHost> create(
        workspace::ViewId view,
        IEditorHostEvents& events) = 0;
};

}
