#include "WxEditorHost.hpp"
#include "WxFileDropTarget.hpp"

#include <wx/panel.h>
#include <wx/scrolbar.h>
#include <wx/settings.h>
#include <wx/stc/stc.h>
#include <wx/event.h>
#include <wx/font.h>
#include <wx/window.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

namespace nff::gui::wxbackend {
namespace {

constexpr int linkIndicator = 7;
constexpr int spoilerIndicator = 8;

[[nodiscard]] int toPixel(const double value) noexcept {
    if (!std::isfinite(value) || value <= 0.0) {
        return 0;
    }
    constexpr auto maximum = static_cast<double>(std::numeric_limits<int>::max());
    return static_cast<int>(std::min(std::round(value), maximum));
}

[[nodiscard]] std::size_t toOffset(const int value) noexcept {
    return value > 0 ? static_cast<std::size_t>(value) : 0U;
}

[[nodiscard]] std::uint64_t toOneBased(const int value) noexcept {
    return value >= 0 ? static_cast<std::uint64_t>(value) + 1U : 1U;
}

[[nodiscard]] std::string utf8(const wxString& value) {
    const auto buffer = value.utf8_str();
    if (buffer.data() == nullptr) {
        return {};
    }
    return {buffer.data(), buffer.length()};
}

[[nodiscard]] std::string viewerErrorText(const std::string_view prefix,
                                          const std::error_code& error) {
    std::string result(prefix);
    if (error) {
        result += "\n\n";
        result += error.message();
    }
    return result;
}

}

WxEditorHost::WxEditorHost(wxWindow& parent,
                           const workspace::ViewId view,
                           IEditorHostEvents& events,
                           const WxThemePalette& theme,
                           const settings::AppSettings& settings,
                           SynchronizeRequest synchronizeRequest,
                           ActivateRequest activateRequest,
                           ContextMenuRequest contextMenuRequest,
                           LinkActivateRequest linkActivateRequest,
                           FileDropRequest fileDropRequest)
    : events_(&events),
      theme_(&theme),
      settings_(&settings),
      synchronizeRequest_(std::move(synchronizeRequest)),
      activateRequest_(std::move(activateRequest)),
      contextMenuRequest_(std::move(contextMenuRequest)),
      linkActivateRequest_(std::move(linkActivateRequest)),
      fileDropRequest_(std::move(fileDropRequest)),
      view_(view),
      fontFamily_(settings.fontFamily),
      fontPointSize_(settings.fontPointSize) {
    container_ = new wxPanel(&parent, wxID_ANY);
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
    container_->SetName(wxString::Format("editor.%llu",
        static_cast<unsigned long long>(view_.value)));
#endif
    control_ = new wxStyledTextCtrl(container_, wxID_ANY);
    externalScroll_ = new wxScrollBar(container_, wxID_ANY,
                                      wxDefaultPosition, wxDefaultSize, wxSB_VERTICAL);

    control_->SetCodePage(wxSTC_CP_UTF8);
    control_->SetMarginType(0, wxSTC_MARGIN_NUMBER);
    control_->SetMarginWidth(0, 0);
    control_->SetWrapMode(wxSTC_WRAP_WORD);
    control_->SetUseHorizontalScrollBar(false);
    externalScroll_->Hide();

    const auto installDropTarget = [this](wxWindow* window) {
        if (window == nullptr) {
            return;
        }
        window->SetDropTarget(new WxFileDropTarget(
            [this](const std::vector<std::filesystem::path>& paths) {
                return fileDropRequest_ && fileDropRequest_(view_, paths);
            }));
    };
    installDropTarget(container_);
    installDropTarget(control_);
    installDropTarget(externalScroll_);

    applyAppearance();
    container_->Bind(wxEVT_LEFT_DOWN, &WxEditorHost::onLeftDown, this);
    control_->Bind(wxEVT_STC_MODIFIED, &WxEditorHost::onModified, this);
    control_->Bind(wxEVT_STC_UPDATEUI, &WxEditorHost::onUpdateUi, this);
    control_->Bind(wxEVT_LEFT_DOWN, &WxEditorHost::onLeftDown, this);
    control_->Bind(wxEVT_MOTION, &WxEditorHost::onMouseMove, this);
    control_->Bind(wxEVT_LEAVE_WINDOW, &WxEditorHost::onMouseLeave, this);
    control_->Bind(wxEVT_CONTEXT_MENU, &WxEditorHost::onContextMenu, this);
    control_->Bind(wxEVT_STC_INDICATOR_CLICK, &WxEditorHost::onIndicatorClick, this);
    control_->Bind(wxEVT_MOUSEWHEEL, &WxEditorHost::onMouseWheel, this);

    externalScroll_->Bind(wxEVT_LEFT_DOWN, &WxEditorHost::onLeftDown, this);
    externalScroll_->Bind(wxEVT_SCROLL_TOP, &WxEditorHost::onViewportScroll, this);
    externalScroll_->Bind(wxEVT_SCROLL_BOTTOM, &WxEditorHost::onViewportScroll, this);
    externalScroll_->Bind(wxEVT_SCROLL_LINEUP, &WxEditorHost::onViewportScroll, this);
    externalScroll_->Bind(wxEVT_SCROLL_LINEDOWN, &WxEditorHost::onViewportScroll, this);
    externalScroll_->Bind(wxEVT_SCROLL_PAGEUP, &WxEditorHost::onViewportScroll, this);
    externalScroll_->Bind(wxEVT_SCROLL_PAGEDOWN, &WxEditorHost::onViewportScroll, this);
    externalScroll_->Bind(wxEVT_SCROLL_THUMBRELEASE, &WxEditorHost::onViewportScroll, this);
    externalScroll_->Bind(wxEVT_SCROLL_CHANGED, &WxEditorHost::onViewportScroll, this);

    container_->Hide();
}

WxEditorHost::~WxEditorHost() {
    if (control_ != nullptr) {
        control_->SetDropTarget(nullptr);
        control_->Unbind(wxEVT_STC_MODIFIED, &WxEditorHost::onModified, this);
        control_->Unbind(wxEVT_STC_UPDATEUI, &WxEditorHost::onUpdateUi, this);
        control_->Unbind(wxEVT_LEFT_DOWN, &WxEditorHost::onLeftDown, this);
        control_->Unbind(wxEVT_MOTION, &WxEditorHost::onMouseMove, this);
        control_->Unbind(wxEVT_LEAVE_WINDOW, &WxEditorHost::onMouseLeave, this);
        control_->Unbind(wxEVT_CONTEXT_MENU, &WxEditorHost::onContextMenu, this);
        control_->Unbind(wxEVT_STC_INDICATOR_CLICK, &WxEditorHost::onIndicatorClick, this);
        control_->Unbind(wxEVT_MOUSEWHEEL, &WxEditorHost::onMouseWheel, this);
    }
    if (externalScroll_ != nullptr) {
        externalScroll_->SetDropTarget(nullptr);
        externalScroll_->Unbind(wxEVT_LEFT_DOWN, &WxEditorHost::onLeftDown, this);
        externalScroll_->Unbind(wxEVT_SCROLL_TOP, &WxEditorHost::onViewportScroll, this);
        externalScroll_->Unbind(wxEVT_SCROLL_BOTTOM, &WxEditorHost::onViewportScroll, this);
        externalScroll_->Unbind(wxEVT_SCROLL_LINEUP, &WxEditorHost::onViewportScroll, this);
        externalScroll_->Unbind(wxEVT_SCROLL_LINEDOWN, &WxEditorHost::onViewportScroll, this);
        externalScroll_->Unbind(wxEVT_SCROLL_PAGEUP, &WxEditorHost::onViewportScroll, this);
        externalScroll_->Unbind(wxEVT_SCROLL_PAGEDOWN, &WxEditorHost::onViewportScroll, this);
        externalScroll_->Unbind(wxEVT_SCROLL_THUMBRELEASE, &WxEditorHost::onViewportScroll, this);
        externalScroll_->Unbind(wxEVT_SCROLL_CHANGED, &WxEditorHost::onViewportScroll, this);
    }
    follower_.reset();
    viewer_.close();
    hex_.close();
    if (container_ != nullptr) {
        container_->SetDropTarget(nullptr);
        container_->Unbind(wxEVT_LEFT_DOWN, &WxEditorHost::onLeftDown, this);
        container_->Destroy();
    }
    container_ = nullptr;
    control_ = nullptr;
    externalScroll_ = nullptr;
}

void WxEditorHost::bind(const EditorHostBinding& binding) {
    if (control_ == nullptr || container_ == nullptr) {
        return;
    }

    const auto previousMode = mode_;
    if (previousMode != binding.mode && binding.mode == core::OpenMode::Editor) {
        forceReload_ = true;
    }
    mode_ = binding.mode;

    const bool wrap = mode_ != core::OpenMode::BinaryPreview && binding.wordWrap;
    if (wordWrapLatch_.update(wrap)) {
        control_->SetWrapMode(wrap ? wxSTC_WRAP_WORD : wxSTC_WRAP_NONE);
        control_->SetUseHorizontalScrollBar(!wrap);
    }

    if (mode_ == core::OpenMode::Editor) {
        bindEditor(binding);
    } else if (mode_ == core::OpenMode::Viewer) {
        bindViewer(binding);
    } else {
        bindHex(binding);
    }

    const bool appearanceChanged = fontFamily_ != binding.fontFamily ||
                                   fontPointSize_ != binding.fontPointSize;
    fontFamily_ = std::string(binding.fontFamily);
    fontPointSize_ = binding.fontPointSize;
    if (appearanceChanged) {
        applyAppearance();
    }

    applyTextAppearance(binding);

    const bool editable = mode_ == core::OpenMode::Editor;
    lineNumbersVisible_ = editable && binding.lineNumbersVisible;
    updateLineNumberMargin(lineNumbersVisible_);

    if (previousMode != mode_) {
        configureModeChrome();
    }
    layoutChildren();
    applyPendingRestore();
    refreshLinkIndicators();
}

void WxEditorHost::bindEditor(const EditorHostBinding& binding) {
    if (!binding.textBufferLoaded) {
        replaceReadOnlyText("This document is not materialized for editing.");
        document_ = binding.document;
        documentRevision_ = binding.documentRevision;
        path_ = binding.path;
        return;
    }

    if (mode_ == core::OpenMode::Editor &&
        (forceReload_ || binding.document != document_ ||
         binding.documentRevision != documentRevision_ || path_ != binding.path)) {
        reloadText(binding);
    }

    if (control_->GetReadOnly()) {
        control_->SetReadOnly(false);
    }
    follower_.reset();
    followEnabled_ = false;
    followWaiting_ = false;
    viewer_.close();
    hex_.close();
    path_ = binding.path;
    viewerByteStart_ = 0U;
    viewerByteEnd_ = 0U;
    viewerFirstLine_ = 1U;
    hexFirstRow_ = 0U;
}

void WxEditorHost::bindViewer(const EditorHostBinding& binding) {
    if (binding.path.empty()) {
        replaceReadOnlyText("View Mode requires a file-backed document.");
        return;
    }

    const bool reopen = !viewer_.isOpen() || path_ != binding.path;
    const bool followChanged = followEnabled_ != binding.followEnabled;
    if (reopen) {
        follower_.reset();
        viewer_.close();
        hex_.close();
        viewer::ViewerOpenOptions options;
        options.performance = binding.viewerPerformance;
        if (settings_ != nullptr) {
            options.inspectOptions = settings_->inspectOptions;
        }
        const auto opened = binding.documentProfile != nullptr &&
                                    binding.documentFileState != nullptr
                                ? viewer_.openPrepared(binding.path,
                                                       *binding.documentProfile,
                                                       *binding.documentFileState,
                                                       options)
                                : viewer_.open(binding.path, options);
        if (!opened) {
            replaceReadOnlyText(viewerErrorText("Unable to open scalable View Mode.", opened.error));
            path_ = binding.path;
            document_ = binding.document;
            documentRevision_ = binding.documentRevision;
            followEnabled_ = false;
            followWaiting_ = false;
            return;
        }
        follower_ = std::make_unique<viewer::LiveFileFollower>(viewer_);
        viewerPerformance_ = binding.viewerPerformance;
        viewerByteStart_ = 0U;
        viewerByteEnd_ = 0U;
        viewerFirstLine_ = 1U;
        path_ = binding.path;
        if (!binding.followEnabled) {
            loadViewerOffset(static_cast<std::uint64_t>(binding.preferredByteOffset),
                             viewer_.recommendedInitialTextWindowBytes());
        }
    } else if (viewerPerformance_ != binding.viewerPerformance) {
        viewer_.setPerformanceProfile(binding.viewerPerformance);
        viewerPerformance_ = binding.viewerPerformance;
        if (!binding.followEnabled) {
            loadViewerOffset(viewerByteStart_);
        }
    }

    followEnabled_ = binding.followEnabled;
    if (!followEnabled_) {
        followWaiting_ = false;
    }
    if (followEnabled_ && (reopen || followChanged)) {
        if (!follower_) {
            follower_ = std::make_unique<viewer::LiveFileFollower>(viewer_);
        }
        const auto tail = follower_->snapshot();
        if (!tail) {
            followWaiting_ = true;
            replaceReadOnlyText(viewerErrorText("Unable to read live file tail.", tail.error));
        } else {
            followWaiting_ = false;
            static_cast<void>(loadFollowTail(tail));
        }
    }

    document_ = binding.document;
    documentRevision_ = binding.documentRevision;
    if (!control_->GetReadOnly()) {
        control_->SetReadOnly(true);
    }
}

void WxEditorHost::bindHex(const EditorHostBinding& binding) {
    if (binding.path.empty()) {
        replaceReadOnlyText("Hex Preview requires a file-backed document.");
        return;
    }

    const bool reopen = !hex_.isOpen() || path_ != binding.path;
    if (reopen) {
        follower_.reset();
        followEnabled_ = false;
        followWaiting_ = false;
        viewer_.close();
        hex_.close();
        const auto error = hex_.open(binding.path);
        if (error) {
            replaceReadOnlyText(viewerErrorText("Unable to open Hex Preview.", error));
            path_ = binding.path;
            document_ = binding.document;
            documentRevision_ = binding.documentRevision;
            return;
        }
        path_ = binding.path;
        hexFirstRow_ = 0U;
        loadHexRow(0U);
    }

    document_ = binding.document;
    documentRevision_ = binding.documentRevision;
    if (!control_->GetReadOnly()) {
        control_->SetReadOnly(true);
    }
}

void WxEditorHost::restoreViewState(const EditorHostViewState& state) {
    pendingRestore_ = state;
    hasPendingRestore_ = true;
}

void WxEditorHost::applyPendingRestore() {
    if (!hasPendingRestore_ || control_ == nullptr) {
        return;
    }

    if (mode_ == core::OpenMode::Viewer && viewer_.isOpen()) {
        loadViewerOffset(static_cast<std::uint64_t>(pendingRestore_.caretOffset));
        hasPendingRestore_ = false;
        return;
    }
    if (mode_ == core::OpenMode::BinaryPreview && hex_.isOpen()) {
        const auto bytesPerRow = static_cast<std::uint64_t>(viewer::HexPreview::defaultBytesPerRow);
        loadHexRow(static_cast<std::uint64_t>(pendingRestore_.caretOffset) / bytesPerRow);
        hasPendingRestore_ = false;
        return;
    }
    if (mode_ != core::OpenMode::Editor) {
        return;
    }

    const auto length = static_cast<std::size_t>(std::max(control_->GetLength(), 0));
    const auto caret = std::min(pendingRestore_.caretOffset, length);
    const auto anchor = std::min(pendingRestore_.anchorOffset, length);
    const auto firstLine = std::min<std::size_t>(pendingRestore_.firstVisibleLine,
                                                 static_cast<std::size_t>(
                                                     std::numeric_limits<int>::max()));
    control_->SetSelection(static_cast<int>(anchor), static_cast<int>(caret));
    control_->ScrollToLine(static_cast<int>(firstLine));
    hasPendingRestore_ = false;
}

void WxEditorHost::setBounds(const Rect bounds) {
    if (container_ == nullptr) {
        return;
    }
    const std::array<int, 4> pixels{toPixel(bounds.x), toPixel(bounds.y),
                                    toPixel(bounds.width), toPixel(bounds.height)};
    if (!containerBoundsLatch_.update(pixels)) {
        return;
    }
    container_->SetSize(pixels[0], pixels[1], pixels[2], pixels[3], wxSIZE_FORCE);
    layoutChildren();
}

void WxEditorHost::setVisible(const bool visible) {
    if (container_ == nullptr || visible_ == visible) {
        return;
    }
    visible_ = visible;
    container_->Show(visible);
}

void WxEditorHost::setFocused(const bool focused) {
    if (control_ == nullptr || focused_ == focused) {
        return;
    }
    focused_ = focused;
    if (focused && visible_) {
        control_->SetFocus();
    }
}

EditorHostRuntime WxEditorHost::runtime() const noexcept {
    EditorHostRuntime result;
    if (control_ == nullptr) {
        return result;
    }

    const auto caret = control_->GetCurrentPos();
    const auto anchor = control_->GetAnchor();
    const auto line = control_->LineFromPosition(caret);
    const auto column = control_->GetColumn(caret);
    const auto selection = caret >= anchor ? caret - anchor : anchor - caret;

    if (mode_ == core::OpenMode::Viewer) {
        result.caretLine = viewerFirstLine_ == 0U
                               ? toOneBased(line)
                               : viewerFirstLine_ + static_cast<std::uint64_t>(std::max(line, 0));
        result.caretColumn = toOneBased(column);
        result.caretOffset = static_cast<std::size_t>(std::min<std::uint64_t>(
            viewerByteStart_, static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())));
        result.anchorOffset = result.caretOffset;
        result.firstVisibleLine = static_cast<std::size_t>(std::min<std::uint64_t>(
            viewerFirstLine_ > 0U ? viewerFirstLine_ - 1U : 0U,
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())));
        result.contentBytes = viewer_.isOpen() ? viewer_.size() : 0U;
        result.windowByteStart = viewerByteStart_;
        result.windowByteEnd = viewerByteEnd_;
        result.resolvedViewerPerformance = viewer_.isOpen()
                                               ? viewer_.performanceProfile()
                                               : viewerPerformance_;
        result.viewerCacheResidentBytes = viewer_.isOpen()
                                              ? viewer_.cacheStatistics().residentBytes
                                              : 0U;
    } else if (mode_ == core::OpenMode::BinaryPreview) {
        result.caretLine = hexFirstRow_ + toOneBased(line);
        result.caretColumn = toOneBased(column);
        const auto absolute = hexFirstRow_ *
                              static_cast<std::uint64_t>(viewer::HexPreview::defaultBytesPerRow);
        result.caretOffset = static_cast<std::size_t>(std::min<std::uint64_t>(
            absolute, static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())));
        result.anchorOffset = result.caretOffset;
        result.firstVisibleLine = static_cast<std::size_t>(std::min<std::uint64_t>(
            hexFirstRow_, static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())));
        result.contentBytes = hex_.isOpen() ? hex_.size() : 0U;
        result.windowByteStart = absolute;
        const auto visibleBytes = hexRowsPerWindow *
                                  static_cast<std::uint64_t>(viewer::HexPreview::defaultBytesPerRow);
        result.windowByteEnd = std::min(result.contentBytes, absolute + visibleBytes);
    } else {
        result.caretLine = toOneBased(line);
        result.caretColumn = toOneBased(column);
        result.caretOffset = toOffset(caret);
        result.anchorOffset = toOffset(anchor);
        result.firstVisibleLine = toOffset(control_->GetFirstVisibleLine());
    }

    result.selectionBytes = static_cast<std::uint64_t>(std::max(selection, 0));
    result.hasSelection = selection != 0;
    result.canUndo = mode_ == core::OpenMode::Editor && control_->CanUndo();
    result.canRedo = mode_ == core::OpenMode::Editor && control_->CanRedo();
    result.canPaste = mode_ == core::OpenMode::Editor && control_->CanPaste();
    result.followWaiting = followEnabled_ && followWaiting_;
    return result;
}

bool WxEditorHost::execute(const EditorHostCommand command) {
    if (control_ == nullptr) {
        return false;
    }

    switch (command) {
    case EditorHostCommand::Undo:
        if (mode_ != core::OpenMode::Editor || !control_->CanUndo()) return false;
        control_->Undo();
        break;
    case EditorHostCommand::Redo:
        if (mode_ != core::OpenMode::Editor || !control_->CanRedo()) return false;
        control_->Redo();
        break;
    case EditorHostCommand::Cut:
        if (mode_ != core::OpenMode::Editor || control_->GetReadOnly() ||
            control_->GetSelectionStart() == control_->GetSelectionEnd()) return false;
        control_->Cut();
        break;
    case EditorHostCommand::Copy:
        if (control_->GetSelectionStart() == control_->GetSelectionEnd()) return false;
        control_->Copy();
        break;
    case EditorHostCommand::Paste:
        if (mode_ != core::OpenMode::Editor || control_->GetReadOnly() || !control_->CanPaste()) {
            return false;
        }
        control_->Paste();
        break;
    case EditorHostCommand::SelectAll:
        control_->SelectAll();
        break;
    }

    requestSynchronize();
    return true;
}

void WxEditorHost::refreshAppearance() {
    applyAppearance();
    restyleTextAppearance();
    forceLinkRefresh_ = true;
    refreshLinkIndicators(true);
}

bool WxEditorHost::recordTextAppearanceUndo(
    const std::uint64_t begin,
    const std::uint64_t end,
    const std::span<const metadata::TextAppearanceSpan> before,
    const std::span<const metadata::TextAppearanceSpan> after) {
    if (control_ == nullptr || mode_ != core::OpenMode::Editor) {
        return false;
    }
    return addTextAppearanceUndoAction(begin, end, before, after, false);
}

EditorHostPollResult WxEditorHost::pollLiveContent() {
    EditorHostPollResult result;
    if (!followEnabled_ || mode_ != core::OpenMode::Viewer || !viewer_.isOpen()) {
        return result;
    }
    if (!follower_) {
        follower_ = std::make_unique<viewer::LiveFileFollower>(viewer_);
    }

    const auto update = follower_->poll();
    if (!update) {
        followWaiting_ = true;
        result.changed = true;
        result.error = update.error;
        return result;
    }
    if (!update.refresh.changed() && !update.hasTail) {
        return result;
    }

    result.changed = true;
    if (update.hasTail) {
        followWaiting_ = false;
        static_cast<void>(loadFollowTail(update.tail));
    } else if (update.refresh.kind == viewer::ViewerRefreshKind::Deleted ||
               update.refresh.kind == viewer::ViewerRefreshKind::Inaccessible) {
        followWaiting_ = true;
    }
    return result;
}

EditorHostSearchResult WxEditorHost::findText(const EditorHostSearchRequest& request) {
    EditorHostSearchResult result;
    if (mode_ != core::OpenMode::Viewer || !viewer_.isOpen()) {
        result.error = std::make_error_code(std::errc::operation_not_supported);
        return result;
    }

    const auto found = viewer_.search(
        request.pattern, request.startOffset, request.direction, request.options);
    result.match = found.match;
    result.bytesScanned = found.bytesScanned;
    result.error = found.error;
    result.wrapped = found.wrapped;
    result.cancelled = found.cancelled;
    return result;
}

bool WxEditorHost::revealTextMatch(const search::SearchMatch match,
                                   const std::string_view pattern) {
    if (control_ == nullptr || pattern.empty()) {
        return false;
    }

    if (mode_ == core::OpenMode::Editor) {
        const auto length = static_cast<std::uint64_t>(std::max(control_->GetLength(), 0));
        if (match.offset > length || match.length > length - match.offset ||
            match.offset > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ||
            match.offset + match.length >
                static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        const auto start = static_cast<int>(match.offset);
        const auto end = static_cast<int>(match.offset + match.length);
        control_->SetSelection(start, end);
        control_->EnsureCaretVisible();
        requestSynchronize();
        return true;
    }

    if (mode_ != core::OpenMode::Viewer || !viewer_.isOpen() ||
        match.offset >= viewer_.size()) {
        return false;
    }

    loadViewerOffset(match.offset);
    if (match.offset < viewerByteStart_ || match.offset >= viewerByteEnd_) {
        return false;
    }

    const auto rawDelta = match.offset - viewerByteStart_;
    std::size_t localOffset = 0U;
    if (rawDelta != 0U) {
        if (rawDelta > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return false;
        }
        const auto prefix = viewer_.readTextWindow(
            viewerByteStart_, static_cast<std::size_t>(rawDelta));
        if (!prefix) {
            return false;
        }
        localOffset = prefix.text.size();
    }

    const auto controlLength = static_cast<std::size_t>(std::max(control_->GetLength(), 0));
    if (localOffset > controlLength || pattern.size() > controlLength - localOffset ||
        localOffset > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        localOffset + pattern.size() >
            static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    control_->SetSelection(static_cast<int>(localOffset),
                           static_cast<int>(localOffset + pattern.size()));
    control_->EnsureCaretVisible();
    requestSynchronize();
    return true;
}

bool WxEditorHost::replaceTextRange(
    const search::SearchMatch match,
    const std::string_view replacement,
    const metadata::TextColorEditPolicy colorPolicy) {
    if (control_ == nullptr || mode_ != core::OpenMode::Editor || control_->GetReadOnly()) {
        return false;
    }

    const auto length = static_cast<std::uint64_t>(std::max(control_->GetLength(), 0));
    if (match.offset > length || match.length > length - match.offset ||
        match.offset > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ||
        match.offset + match.length >
            static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    const auto replacementBytes = static_cast<std::uint64_t>(replacement.size());
    if (replacementBytes > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) -
                               match.offset ||
        replacementBytes >
            static_cast<std::uint64_t>(std::numeric_limits<int>::max()) -
                (length - match.length)) {
        return false;
    }

    const auto start = static_cast<int>(match.offset);
    const auto end = static_cast<int>(match.offset + match.length);
    const auto value = wxString::FromUTF8(replacement.data(), replacement.size());
    control_->BeginUndoAction();
    suppressModified_ = true;
    control_->SetTargetStart(start);
    control_->SetTargetEnd(end);
    const auto inserted = control_->ReplaceTarget(value);
    suppressModified_ = false;
    if (inserted < 0) {
        control_->EndUndoAction();
        return false;
    }

    const EditorTextEdit edit{
        static_cast<std::size_t>(match.offset),
        static_cast<std::size_t>(match.length),
        replacement,
        documentRevision_,
        colorPolicy};
    if (events_ == nullptr || !events_->applyTextEdit(view_, edit)) {
        forceReload_ = true;
        control_->EndUndoAction();
        requestSynchronize();
        return false;
    }
    ++documentRevision_;
    forceLinkRefresh_ = true;
    updateLineNumberMargin(lineNumbersVisible_);
    noteLocalAppearanceEdit(edit, false);
    control_->EndUndoAction();

    control_->SetSelection(start, start + static_cast<int>(replacementBytes));
    control_->EnsureCaretVisible();
    requestSynchronize();
    return true;
}

bool WxEditorHost::replaceAllText(
    const std::string_view text,
    const metadata::TextColorEditPolicy colorPolicy) {
    if (control_ == nullptr || mode_ != core::OpenMode::Editor || control_->GetReadOnly() ||
        text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    const auto oldLength = std::max(control_->GetLength(), 0);
    const auto caret = std::clamp(control_->GetCurrentPos(), 0, oldLength);
    const auto anchor = std::clamp(control_->GetAnchor(), 0, oldLength);
    const auto value = wxString::FromUTF8(text.data(), text.size());

    control_->BeginUndoAction();
    suppressModified_ = true;
    control_->SetTargetStart(0);
    control_->SetTargetEnd(oldLength);
    const auto inserted = control_->ReplaceTarget(value);
    suppressModified_ = false;
    if (inserted < 0) {
        control_->EndUndoAction();
        return false;
    }

    const EditorTextEdit edit{
        0U,
        static_cast<std::size_t>(oldLength),
        text,
        documentRevision_,
        colorPolicy};
    if (events_ == nullptr || !events_->applyTextEdit(view_, edit)) {
        forceReload_ = true;
        control_->EndUndoAction();
        requestSynchronize();
        return false;
    }
    ++documentRevision_;
    forceLinkRefresh_ = true;
    updateLineNumberMargin(lineNumbersVisible_);
    noteLocalAppearanceEdit(edit, false);
    control_->EndUndoAction();

    const auto newLength = std::max(control_->GetLength(), 0);
    control_->SetSelection(std::clamp(anchor, 0, newLength),
                           std::clamp(caret, 0, newLength));
    control_->EnsureCaretVisible();
    requestSynchronize();
    return true;
}

std::error_code WxEditorHost::goToLine(const std::uint64_t oneBasedLine) {
    if (control_ == nullptr || oneBasedLine == 0U) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    if (mode_ == core::OpenMode::Editor) {
        const auto totalLines = static_cast<std::uint64_t>(std::max(control_->GetLineCount(), 1));
        if (oneBasedLine > totalLines) {
            return std::make_error_code(std::errc::result_out_of_range);
        }
        const auto target = oneBasedLine - 1U;
        if (target > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            return std::make_error_code(std::errc::value_too_large);
        }
        control_->GotoLine(static_cast<int>(target));
        control_->EnsureCaretVisible();
        requestSynchronize();
        return {};
    }

    if (mode_ == core::OpenMode::Viewer && viewer_.isOpen()) {
        const auto line = viewer_.lineStart(oneBasedLine);
        if (!line) {
            return line.error;
        }
        if (!line.offset) {
            return std::make_error_code(std::errc::result_out_of_range);
        }
        loadViewerOffset(*line.offset);
        const auto localLine = oneBasedLine > viewerFirstLine_
                                   ? oneBasedLine - viewerFirstLine_
                                   : 0U;
        if (localLine <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            control_->GotoLine(static_cast<int>(localLine));
            control_->EnsureCaretVisible();
        }
        requestSynchronize();
        return {};
    }

    if (mode_ == core::OpenMode::BinaryPreview && hex_.isOpen()) {
        const auto totalRows = hex_.rowCount();
        if (oneBasedLine > totalRows && totalRows != 0U) {
            return std::make_error_code(std::errc::result_out_of_range);
        }
        const auto targetRow = oneBasedLine - 1U;
        loadHexRow(targetRow);
        const auto localRow = targetRow >= hexFirstRow_ ? targetRow - hexFirstRow_ : 0U;
        if (localRow <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            control_->GotoLine(static_cast<int>(localRow));
            control_->EnsureCaretVisible();
        }
        requestSynchronize();
        return {};
    }

    return std::make_error_code(std::errc::operation_not_supported);
}

std::error_code WxEditorHost::goToPosition(const std::uint64_t oneBasedLine,
                                           const std::uint64_t oneBasedColumn) {
    if (oneBasedColumn == 0U) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    const auto lineError = goToLine(oneBasedLine);
    if (lineError || oneBasedColumn == 1U || control_ == nullptr) {
        return lineError;
    }

    const auto zeroBasedColumn = oneBasedColumn - 1U;
    if (zeroBasedColumn > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        return std::make_error_code(std::errc::value_too_large);
    }
    const auto currentLine = control_->LineFromPosition(control_->GetCurrentPos());
    if (currentLine < 0) {
        return std::make_error_code(std::errc::result_out_of_range);
    }
    const auto position = control_->FindColumn(
        currentLine, static_cast<int>(zeroBasedColumn));
    if (position < 0) {
        return std::make_error_code(std::errc::result_out_of_range);
    }
    control_->GotoPos(position);
    control_->EnsureCaretVisible();
    requestSynchronize();
    return {};
}

void WxEditorHost::onModified(wxStyledTextEvent& event) {
    if (mode_ != core::OpenMode::Editor || suppressModified_ || events_ == nullptr) {
        event.Skip();
        return;
    }

    const auto modification = event.GetModificationType();
    if ((modification & wxSTC_MOD_CONTAINER) != 0) {
        if ((modification & wxSTC_PERFORMED_UNDO) != 0) {
            static_cast<void>(restoreTextAppearanceUndo(
                event.GetToken(), TextAppearanceUndoDirection::Undo));
        } else if ((modification & wxSTC_PERFORMED_REDO) != 0) {
            static_cast<void>(restoreTextAppearanceUndo(
                event.GetToken(), TextAppearanceUndoDirection::Redo));
        }
        requestSynchronize();
        event.Skip();
        return;
    }

    const bool inserted = (modification & wxSTC_MOD_INSERTTEXT) != 0;
    const bool deleted = (modification & wxSTC_MOD_DELETETEXT) != 0;
    if (inserted == deleted) {
        event.Skip();
        return;
    }

    EditorTextEdit edit;
    edit.offset = toOffset(event.GetPosition());
    edit.baseRevision = documentRevision_;
    const bool undoOrRedo = (modification & (wxSTC_PERFORMED_UNDO | wxSTC_PERFORMED_REDO)) != 0;
    edit.colorPolicy = undoOrRedo
                           ? metadata::TextColorEditPolicy::PreserveState
                           : metadata::TextColorEditPolicy::AdjustRanges;

    std::string insertedText;
    if (inserted) {
        insertedText = utf8(event.GetText());
        edit.insertedText = insertedText;
    } else {
        edit.eraseBytes = toOffset(event.GetLength());
    }

    if (events_->applyTextEdit(view_, edit)) {
        ++documentRevision_;
        forceLinkRefresh_ = true;
        updateLineNumberMargin(lineNumbersVisible_);

        if (!undoOrRedo) {
            noteLocalAppearanceEdit(edit, true);
        }
    } else {
        forceReload_ = true;
    }

    refreshLinkIndicators();
    requestSynchronize();
    event.Skip();
}

void WxEditorHost::onLeftDown(wxMouseEvent& event) {
    if (activateRequest_) {
        activateRequest_(view_);
    }
    event.Skip();
}

void WxEditorHost::onMouseMove(wxMouseEvent& event) {
    if (control_ != nullptr && hasSpoilers_) {
        updateSpoilerRevealAt(control_->PositionFromPoint(event.GetPosition()));
    }
    event.Skip();
}

void WxEditorHost::onMouseLeave(wxMouseEvent& event) {
    if (hasSpoilers_) {
        updateSpoilerRevealAt(-1);
    }
    event.Skip();
}

void WxEditorHost::onUpdateUi(wxStyledTextEvent& event) {
    refreshLinkIndicators();
    requestSynchronize();
    event.Skip();
}

void WxEditorHost::onContextMenu(wxContextMenuEvent& event) {
    if (control_ != nullptr && contextMenuRequest_) {
        contextMenuRequest_(*control_, view_);
        return;
    }
    event.Skip();
}

void WxEditorHost::onIndicatorClick(wxStyledTextEvent& event) {
    if (control_ == nullptr || settings_ == nullptr || !settings_->highlightUrls ||
        mode_ == core::OpenMode::BinaryPreview || !event.GetControl()) {
        event.Skip();
        return;
    }

    const int position = event.GetPosition();
    if (position < 0 || (control_->IndicatorAllOnFor(position) & (1 << linkIndicator)) == 0) {
        event.Skip();
        return;
    }

    const auto iterator = std::find_if(visibleLinks_.begin(), visibleLinks_.end(),
                                       [position](const core::LinkSpan& link) {
        const auto begin = static_cast<std::uint64_t>(link.offset);
        const auto end = begin + static_cast<std::uint64_t>(link.length);
        const auto clicked = static_cast<std::uint64_t>(position);
        return clicked >= begin && clicked < end;
    });
    if (iterator == visibleLinks_.end() || !linkActivateRequest_) {
        event.Skip();
        return;
    }

    linkActivateRequest_(view_, *iterator);
}

void WxEditorHost::onViewportScroll(wxScrollEvent& event) {
    if (suppressExternalScroll_) {
        event.Skip();
        return;
    }

    const auto position = event.GetPosition();
    if (mode_ == core::OpenMode::Viewer && viewer_.isOpen()) {
        const auto windowBytes = static_cast<std::uint64_t>(viewerWindowBytes());
        const auto maximumStart = viewer_.size() > windowBytes ? viewer_.size() - windowBytes : 0U;
        loadViewerOffset(scrollPositionToOffset(position, maximumStart));
    } else if (mode_ == core::OpenMode::BinaryPreview && hex_.isOpen()) {
        const auto totalRows = hex_.rowCount();
        const auto maximumStart = totalRows > hexRowsPerWindow
                                      ? totalRows - hexRowsPerWindow
                                      : 0U;
        loadHexRow(scrollPositionToOffset(position, maximumStart));
    }
    requestSynchronize();
    event.Skip();
}

void WxEditorHost::onMouseWheel(wxMouseEvent& event) {
    if (mode_ != core::OpenMode::Viewer || !viewer_.isOpen() || control_ == nullptr ||
        event.GetWheelRotation() == 0 || event.GetWheelDelta() == 0) {
        event.Skip();
        return;
    }

    const auto firstVisible = std::max(control_->GetFirstVisibleLine(), 0);
    const auto linesOnScreen = std::max(control_->LinesOnScreen(), 1);
    const auto displayLines = viewerDisplayLineCount();
    const bool movingBackward = event.GetWheelRotation() > 0;
    const bool atBoundary = movingBackward
                                ? firstVisible <= 0
                                : firstVisible + linesOnScreen >= displayLines;
    if (!atBoundary) {
        event.Skip();
        return;
    }

    const auto currentBytes = viewerByteEnd_ > viewerByteStart_
                                  ? viewerByteEnd_ - viewerByteStart_
                                  : static_cast<std::uint64_t>(viewerWindowBytes());
    const auto shift = std::max<std::uint64_t>(currentBytes / 2U, 1U);
    std::uint64_t target = viewerByteStart_;
    if (movingBackward) {
        if (viewerByteStart_ == 0U) {
            event.Skip();
            return;
        }
        target = viewerByteStart_ > shift ? viewerByteStart_ - shift : 0U;
    } else {
        if (viewerByteEnd_ >= viewer_.size()) {
            event.Skip();
            return;
        }
        target = std::min(viewerByteStart_ + shift, viewer_.size() - 1U);
    }

    const auto previousStart = viewerByteStart_;
    loadViewerOffset(target, static_cast<std::size_t>(std::min<std::uint64_t>(
                                 currentBytes,
                                 static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))));
    if (viewerByteStart_ == previousStart) {
        event.Skip();
        return;
    }

    positionViewerAfterWindowShift(movingBackward);
    requestSynchronize();

}

void WxEditorHost::requestSynchronize() {
    if (synchronizeRequest_) {
        synchronizeRequest_();
    }
}

void WxEditorHost::replaceReadOnlyText(const std::string& text) {
    if (control_ == nullptr) {
        return;
    }
    const bool wasReadOnly = control_->GetReadOnly();
    suppressModified_ = true;
    if (wasReadOnly) {
        control_->SetReadOnly(false);
    }
    control_->SetText(wxString::FromUTF8(text.data(), text.size()));
    control_->EmptyUndoBuffer();
    appearanceUndoJournal_.clear();
    control_->SetSavePoint();
    control_->SetSelection(0, 0);
    control_->ScrollToLine(0);
    control_->SetReadOnly(true);
    suppressModified_ = false;
    forceLinkRefresh_ = true;
}

void WxEditorHost::loadViewerOffset(const std::uint64_t byteOffset,
                                    const std::size_t maximumBytes) {
    if (!viewer_.isOpen()) {
        return;
    }
    const auto normalWindowBytes = static_cast<std::uint64_t>(viewerWindowBytes());
    const auto requestedWindowBytes = maximumBytes == 0U
                                          ? normalWindowBytes
                                          : std::min<std::uint64_t>(
                                                normalWindowBytes,
                                                static_cast<std::uint64_t>(maximumBytes));
    const auto maximumStart = viewer_.size() > normalWindowBytes
                                  ? viewer_.size() - normalWindowBytes
                                  : 0U;
    const auto clamped = std::min(byteOffset, maximumStart);
    if (viewerByteEnd_ > viewerByteStart_ && clamped == viewerByteStart_) {
        updateExternalScrollbar(clamped, maximumStart);
        return;
    }

    const auto window = viewer_.readTextWindow(
        clamped, static_cast<std::size_t>(requestedWindowBytes));
    if (!window) {
        if (window.error == std::make_error_code(std::errc::illegal_byte_sequence)) {
            replaceReadOnlyText(
                "This View Mode window is not valid in the selected text encoding.\n"
                "Use View > Content Mode > Hex Preview to inspect the raw bytes, or "
                "File > Reopen As Encoding to choose the correct source encoding.");
        } else {
            replaceReadOnlyText(viewerErrorText("Unable to read this View Mode window.", window.error));
        }
        return;
    }
    viewerByteStart_ = window.byteStart;
    viewerByteEnd_ = window.byteEnd;
    viewerFirstLine_ = window.firstLine;
    replaceReadOnlyText(window.text);
    updateExternalScrollbar(viewerByteStart_, maximumStart);
}

bool WxEditorHost::loadFollowTail(const viewer::TailWindowResult& tail) {
    if (!viewer_.isOpen() || control_ == nullptr) {
        return false;
    }

    viewerByteStart_ = tail.byteStart;
    viewerByteEnd_ = tail.byteEnd;
    const auto line = viewer_.lineAtOffset(tail.byteStart);
    viewerFirstLine_ = line && line.line ? *line.line : 1U;
    replaceReadOnlyText(tail.text);

    const auto length = std::max(control_->GetLength(), 0);
    control_->SetSelection(length, length);
    const auto lineCount = std::max(control_->GetLineCount(), 1);
    control_->ScrollToLine(lineCount - 1);

    const auto windowBytes = static_cast<std::uint64_t>(viewerWindowBytes());
    const auto maximumStart = viewer_.size() > windowBytes ? viewer_.size() - windowBytes : 0U;
    updateExternalScrollbar(maximumStart, maximumStart);
    return true;
}

void WxEditorHost::loadHexRow(const std::uint64_t firstRow) {
    if (!hex_.isOpen()) {
        return;
    }
    const auto totalRows = hex_.rowCount();
    const auto maximumStart = totalRows > hexRowsPerWindow
                                  ? totalRows - hexRowsPerWindow
                                  : 0U;
    const auto clamped = std::min(firstRow, maximumStart);
    if (clamped == hexFirstRow_ && control_ != nullptr && control_->GetLength() != 0) {
        updateExternalScrollbar(clamped, totalRows);
        return;
    }

    const auto window = hex_.readRows(clamped, hexRowsPerWindow);
    if (!window) {
        replaceReadOnlyText(viewerErrorText("Unable to read this Hex Preview window.", window.error));
        return;
    }
    hexFirstRow_ = window.firstRow;
    replaceReadOnlyText(renderHexWindow(window));
    updateExternalScrollbar(hexFirstRow_, maximumStart);
}

std::string WxEditorHost::renderHexWindow(const viewer::HexWindow& window) const {
    std::ostringstream output;
    const auto maximumOffset = hex_.size() == 0U ? 0U : hex_.size() - 1U;
    const int addressWidth = maximumOffset > 0xFFFFFFFFULL ? 16 : 8;
    output << std::uppercase << std::hex << std::setfill('0');

    for (std::uint64_t relativeRow = 0U; relativeRow < window.rowCount; ++relativeRow) {
        const auto row = window.row(relativeRow);
        const auto absoluteRow = window.firstRow + relativeRow;
        const auto offset = absoluteRow * static_cast<std::uint64_t>(window.bytesPerRow);
        output << std::setw(addressWidth) << offset << "  ";

        for (std::size_t index = 0U; index < window.bytesPerRow; ++index) {
            if (index < row.size()) {
                output << std::setw(2) << std::to_integer<unsigned int>(row[index]) << ' ';
            } else {
                output << "   ";
            }
            if (index == 7U) {
                output << ' ';
            }
        }

        output << " |";
        for (const auto value : row) {
            output << viewer::HexPreview::printableAscii(value);
        }
        for (std::size_t index = row.size(); index < window.bytesPerRow; ++index) {
            output << ' ';
        }
        output << "|\n";
    }
    return output.str();
}

std::size_t WxEditorHost::viewerWindowBytes() const noexcept {
    if (!viewer_.isOpen()) {
        return 512U * 1024U;
    }
    return viewer_.performanceProfile() == viewer::PerformanceProfile::Fast
               ? 2U * 1024U * 1024U
               : 512U * 1024U;
}

std::uint64_t WxEditorHost::scrollPositionToOffset(const int position,
                                                   const std::uint64_t maximum) const noexcept {
    if (maximum == 0U) {
        return 0U;
    }
    constexpr auto scrollSpan = virtualScrollRange - virtualScrollThumb;
    const auto clamped = std::clamp(position, 0, scrollSpan);
    const auto numerator = static_cast<long double>(clamped) * static_cast<long double>(maximum);
    const auto value = numerator / static_cast<long double>(scrollSpan);
    return static_cast<std::uint64_t>(std::min<long double>(value, maximum));
}

int WxEditorHost::offsetToScrollPosition(const std::uint64_t offset,
                                         const std::uint64_t maximum) const noexcept {
    if (maximum == 0U) {
        return 0;
    }
    constexpr auto scrollSpan = virtualScrollRange - virtualScrollThumb;
    const auto clamped = std::min(offset, maximum);
    const auto numerator = static_cast<long double>(clamped) * static_cast<long double>(scrollSpan);
    const auto value = numerator / static_cast<long double>(maximum);
    return std::clamp(static_cast<int>(value), 0, scrollSpan);
}

void WxEditorHost::updateExternalScrollbar(const std::uint64_t offset,
                                           const std::uint64_t maximum) {
    if (externalScroll_ == nullptr) {
        return;
    }
    suppressExternalScroll_ = true;
    externalScroll_->SetScrollbar(offsetToScrollPosition(offset, maximum),
                                  virtualScrollThumb,
                                  virtualScrollRange,
                                  virtualScrollPage,
                                  true);
    suppressExternalScroll_ = false;
}

int WxEditorHost::viewerDisplayLineCount() const noexcept {
    if (control_ == nullptr) {
        return 1;
    }
    const auto documentLines = std::max(control_->GetLineCount(), 1);
    const auto lastDocumentLine = documentLines - 1;
    const auto firstDisplayLine = std::max(control_->VisibleFromDocLine(lastDocumentLine), 0);
    const auto wrappedLines = std::max(control_->WrapCount(lastDocumentLine), 1);
    return std::max(firstDisplayLine + wrappedLines, 1);
}

void WxEditorHost::positionViewerAfterWindowShift(const bool movingBackward) {
    if (control_ == nullptr) {
        return;
    }
    const auto displayLines = viewerDisplayLineCount();
    const auto linesOnScreen = std::max(control_->LinesOnScreen(), 1);
    const auto midpoint = displayLines / 2;
    const auto target = movingBackward
                            ? std::max(midpoint - linesOnScreen, 0)
                            : std::max(midpoint, 0);
    control_->SetFirstVisibleLine(std::min(target, std::max(displayLines - linesOnScreen, 0)));
}

void WxEditorHost::configureModeChrome() {
    if (control_ == nullptr || externalScroll_ == nullptr) {
        return;
    }
    const bool external = mode_ != core::OpenMode::Editor;
    externalScroll_->Show(external);
    control_->SetUseVerticalScrollBar(!external);
    if (mode_ != core::OpenMode::Editor && !control_->GetReadOnly()) {
        control_->SetReadOnly(true);
    }
    lineNumberMarginWidth_ = -1;
    updateLineNumberMargin(lineNumbersVisible_ && mode_ == core::OpenMode::Editor);
    layoutChildren();
}

void WxEditorHost::layoutChildren() {
    if (container_ == nullptr || control_ == nullptr || externalScroll_ == nullptr) {
        return;
    }
    const auto size = container_->GetClientSize();
    int scrollWidth = 0;
    if (externalScroll_->IsShown()) {
        scrollWidth = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, container_);
        if (scrollWidth <= 0) {
            scrollWidth = 16;
        }
    }
    const auto contentWidth = std::max(size.GetWidth() - scrollWidth, 0);
    const auto contentHeight = std::max(size.GetHeight(), 0);
    const std::array<int, 4> controlBounds{0, 0, contentWidth, contentHeight};
    if (controlBoundsLatch_.update(controlBounds)) {
        control_->SetSize(0, 0, contentWidth, contentHeight, wxSIZE_FORCE);
    }
    if (externalScroll_->IsShown()) {
        const std::array<int, 4> scrollBounds{contentWidth, 0, scrollWidth, contentHeight};
        if (scrollBoundsLatch_.update(scrollBounds)) {
            externalScroll_->SetSize(contentWidth, 0, scrollWidth, contentHeight, wxSIZE_FORCE);
        }
    } else {
        scrollBoundsLatch_.invalidate();
    }
}

void WxEditorHost::applyAppearance() {
    if (control_ == nullptr || theme_ == nullptr || settings_ == nullptr) {
        return;
    }

    const auto& theme = *theme_;
    if (container_ != nullptr) {
        container_->SetBackgroundColour(theme.editor);
    }
    control_->SetBackgroundColour(theme.editor);
    control_->SetForegroundColour(theme.text);

    control_->StyleResetDefault();
    control_->StyleSetBackground(wxSTC_STYLE_DEFAULT, theme.editor);
    control_->StyleSetForeground(wxSTC_STYLE_DEFAULT, theme.text);
    if (!fontFamily_.empty()) {
        control_->StyleSetFaceName(wxSTC_STYLE_DEFAULT,
                                   wxString::FromUTF8(fontFamily_.c_str()));
    }
    control_->StyleSetSize(
        wxSTC_STYLE_DEFAULT,
        static_cast<int>(std::round(std::max(1.0, fontPointSize_))));
    control_->StyleClearAll();

    appearanceStyleRefresh_.invalidateAppearance();
    control_->SetCaretForeground(theme.text);
    control_->SetSelBackground(true, theme.selection);
    control_->SetSelForeground(true, theme.text);
    control_->StyleSetBackground(wxSTC_STYLE_LINENUMBER, theme.lineNumberBackground);
    control_->StyleSetForeground(wxSTC_STYLE_LINENUMBER, theme.lineNumber);
    const auto lineNumberFont = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    if (lineNumberFont.IsOk()) {
        const auto faceName = lineNumberFont.GetFaceName();
        if (!faceName.empty()) {
            control_->StyleSetFaceName(wxSTC_STYLE_LINENUMBER, faceName);
        }
        control_->StyleSetSize(wxSTC_STYLE_LINENUMBER,
                               std::max(1, lineNumberFont.GetPointSize()));
        control_->StyleSetBold(wxSTC_STYLE_LINENUMBER, false);
        control_->StyleSetItalic(wxSTC_STYLE_LINENUMBER, false);
    }
    control_->SetWhitespaceForeground(true, theme.border);
    control_->IndicatorSetStyle(linkIndicator, wxSTC_INDIC_PLAIN);
    control_->IndicatorSetForeground(linkIndicator, theme.accent);
    control_->IndicatorSetHoverStyle(linkIndicator, wxSTC_INDIC_PLAIN);
    control_->IndicatorSetHoverForeground(linkIndicator, theme.accentHover);
    control_->IndicatorSetUnder(linkIndicator, true);
    control_->IndicatorSetStyle(spoilerIndicator, wxSTC_INDIC_FULLBOX);
    control_->IndicatorSetForeground(spoilerIndicator, theme.editor);
    control_->IndicatorSetAlpha(spoilerIndicator, 255);
    control_->IndicatorSetOutlineAlpha(spoilerIndicator, 255);
    control_->IndicatorSetUnder(spoilerIndicator, false);
    const int verticalPadding = settings_->density == settings::UiDensity::Compact ? 1 : 2;
    control_->SetExtraAscent(verticalPadding);
    control_->SetExtraDescent(verticalPadding);
    lineNumberMarginWidth_ = -1;
    updateLineNumberMargin(lineNumbersVisible_ && mode_ == core::OpenMode::Editor);
    control_->Refresh(false);
    if (container_ != nullptr) {
        container_->Refresh(false);
    }
}

metadata::TextAppearanceMap WxEditorHost::currentAppearanceMap() const {
    metadata::TextAppearanceMap appearance;
    static_cast<void>(appearance.replace(appearanceSpans_, appearanceFontFamilies_));
    return appearance;
}

metadata::TextAppearanceMap WxEditorHost::adjustedTextAppearance(
    const EditorTextEdit& edit) const {
    auto adjusted = currentAppearanceMap();
    adjusted.applyEdit(edit.offset, edit.eraseBytes, edit.insertedText.size(), edit.colorPolicy);
    return adjusted;
}

void WxEditorHost::noteLocalAppearanceEdit(const EditorTextEdit& edit,
                                           const bool mayCoalesce) {
    const auto beforeMap = currentAppearanceMap();
    auto afterMap = beforeMap;
    afterMap.applyEdit(edit.offset, edit.eraseBytes, edit.insertedText.size(), edit.colorPolicy);
    if (beforeMap.spans() == afterMap.spans()) {
        return;
    }

    const std::uint64_t begin = static_cast<std::uint64_t>(edit.offset);
    std::uint64_t end = begin + 1U;
    for (const auto& span : beforeMap.spans()) {
        if (span.end > begin) {
            end = std::max(end, span.end);
        }
    }
    for (const auto& span : afterMap.spans()) {
        if (span.end > begin) {
            end = std::max(end, span.end);
        }
    }
    const auto before = beforeMap.fragment(begin, end);
    const auto after = afterMap.fragment(begin, end);
    static_cast<void>(addTextAppearanceUndoAction(begin, end, before, after, mayCoalesce));

    clearAppearanceStyles();
    appearanceSpans_ = afterMap.spans();
    hasSpoilers_ = afterMap.hasSpoilers();
    revealedSpoiler_.reset();
    ++appearanceRevision_;
    appearanceStyleRefresh_.invalidateAppearance();
    restyleTextAppearance();
}

bool WxEditorHost::addTextAppearanceUndoAction(
    const std::uint64_t begin,
    const std::uint64_t end,
    const std::span<const metadata::TextAppearanceSpan> before,
    const std::span<const metadata::TextAppearanceSpan> after,
    const bool mayCoalesce) {
    if (control_ == nullptr) {
        return false;
    }
    const int token = appearanceUndoJournal_.record(begin, end, before, after);
    if (token <= 0) {
        return before.size() == after.size() &&
               std::equal(before.begin(), before.end(), after.begin());
    }
    control_->AddUndoAction(token, mayCoalesce ? wxSTC_UNDO_MAY_COALESCE : 0);
    return true;
}

bool WxEditorHost::restoreTextAppearanceUndo(
    const int token,
    const TextAppearanceUndoDirection direction) {
    const auto* delta = appearanceUndoJournal_.delta(token, direction);
    if (delta == nullptr || events_ == nullptr ||
        !events_->restoreTextAppearanceRange(view_, delta->begin, delta->end, delta->spans)) {
        return false;
    }

    auto appearance = currentAppearanceMap();
    if (!appearance.replaceRange(delta->begin, delta->end, delta->spans)) {

        appearanceStyleRefresh_.invalidateAppearance();
        requestSynchronize();
        return true;
    }
    clearAppearanceStyles();
    appearanceSpans_ = appearance.spans();
    hasSpoilers_ = appearance.hasSpoilers();
    revealedSpoiler_.reset();
    ++appearanceRevision_;
    appearanceStyleRefresh_.invalidateAppearance();
    restyleTextAppearance();
    return true;
}

void WxEditorHost::applyTextAppearance(const EditorHostBinding& binding) {
    if (control_ == nullptr) {
        return;
    }
    if (mode_ != core::OpenMode::Editor) {
        clearAppearanceStyles();
        appearanceSpans_.clear();
        appearanceFontFamilies_.clear();
        hasSpoilers_ = false;
        revealedSpoiler_.reset();
        appearanceDocument_ = binding.document;
        appearanceRevision_ = binding.appearanceRevision;
        appearanceStyleRefresh_.invalidateAppearance();
        return;
    }

    if (!appearanceStyleRefresh_.needsRefresh() && appearanceDocument_ == binding.document &&
        appearanceRevision_ == binding.appearanceRevision) {
        return;
    }

    clearAppearanceStyles();
    const bool documentChanged = appearanceDocument_ != binding.document;
    appearanceDocument_ = binding.document;
    appearanceRevision_ = binding.appearanceRevision;
    appearanceSpans_.assign(binding.appearanceSpans.begin(), binding.appearanceSpans.end());
    appearanceFontFamilies_.assign(binding.appearanceFontFamilies.begin(),
                                   binding.appearanceFontFamilies.end());
    hasSpoilers_ = binding.hasSpoilers;
    revealedSpoiler_.reset();
    if (documentChanged) {
        appearanceUndoJournal_.clear();
    }
    restyleTextAppearance();
}

void WxEditorHost::clearAppearanceStyles() {
    if (control_ == nullptr) {
        return;
    }
    const auto rawLength = std::max(control_->GetLength(), 0);
    for (const auto& span : appearanceSpans_) {
        const auto begin64 = std::min<std::uint64_t>(span.begin,
            static_cast<std::uint64_t>(rawLength));
        const auto end64 = std::min<std::uint64_t>(span.end,
            static_cast<std::uint64_t>(rawLength));
        if (begin64 >= end64 || end64 > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            continue;
        }
        if (span.style.foregroundArgb || span.style.fontFamilyId || span.style.fontSizePoints) {
            control_->StartStyling(static_cast<int>(begin64));
            control_->SetStyling(static_cast<int>(end64 - begin64), wxSTC_STYLE_DEFAULT);
        }
        if (span.style.spoiler) {
            control_->SetIndicatorCurrent(spoilerIndicator);
            control_->IndicatorClearRange(static_cast<int>(begin64),
                                          static_cast<int>(end64 - begin64));
        }
    }
}

void WxEditorHost::restyleTextAppearance() {
    if (control_ == nullptr || mode_ != core::OpenMode::Editor || theme_ == nullptr) {
        return;
    }

    const auto rawLength = std::max(control_->GetLength(), 0);
    constexpr int firstAppearanceStyle = 64;
    constexpr int lastAppearanceStyle = 191;
    using StyleKey = std::tuple<std::optional<std::uint32_t>,
                                std::optional<metadata::FontFamilyId>,
                                std::optional<std::uint8_t>>;
    std::map<StyleKey, int> styles;
    int nextStyle = firstAppearanceStyle;

    for (const auto& span : appearanceSpans_) {
        if (span.begin >= span.end || span.begin >= static_cast<std::uint64_t>(rawLength)) {
            continue;
        }
        const auto end64 = std::min<std::uint64_t>(span.end,
            static_cast<std::uint64_t>(rawLength));
        if (end64 > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            continue;
        }
        const int begin = static_cast<int>(span.begin);
        const int length = static_cast<int>(end64 - span.begin);
        if (length <= 0) {
            continue;
        }

        if (span.style.foregroundArgb || span.style.fontFamilyId || span.style.fontSizePoints) {
            const StyleKey key{span.style.foregroundArgb,
                               span.style.fontFamilyId,
                               span.style.fontSizePoints};
            auto iterator = styles.find(key);
            if (iterator == styles.end()) {
                if (nextStyle > lastAppearanceStyle) {

                    continue;
                }
                const int style = nextStyle++;
                control_->StyleSetForeground(style, theme_->text);
                control_->StyleSetBackground(style, theme_->editor);
                if (!fontFamily_.empty()) {
                    control_->StyleSetFaceName(style, wxString::FromUTF8(fontFamily_));
                }
                control_->StyleSetSize(style,
                    static_cast<int>(std::round(std::max(1.0, fontPointSize_))));

                if (span.style.foregroundArgb) {
                    const auto argb = *span.style.foregroundArgb;
                    control_->StyleSetForeground(
                        style,
                        wxColour(static_cast<unsigned char>((argb >> 16U) & 0xFFU),
                                 static_cast<unsigned char>((argb >> 8U) & 0xFFU),
                                 static_cast<unsigned char>(argb & 0xFFU)));
                }
                if (span.style.fontFamilyId &&
                    *span.style.fontFamilyId < appearanceFontFamilies_.size()) {
                    control_->StyleSetFaceName(
                        style, wxString::FromUTF8(appearanceFontFamilies_[*span.style.fontFamilyId]));
                }
                if (span.style.fontSizePoints) {
                    control_->StyleSetSize(style, static_cast<int>(*span.style.fontSizePoints));
                }
                iterator = styles.emplace(key, style).first;
            }
            control_->StartStyling(begin);
            control_->SetStyling(length, iterator->second);
        }
    }

    refreshSpoilerIndicators();
    appearanceStyleRefresh_.markRendered();
    control_->Refresh(false);
}

void WxEditorHost::refreshSpoilerIndicators() {
    if (control_ == nullptr || mode_ != core::OpenMode::Editor) {
        return;
    }
    const auto rawLength = std::max(control_->GetLength(), 0);
    control_->SetIndicatorCurrent(spoilerIndicator);
    for (const auto& span : appearanceSpans_) {
        if (!span.style.spoiler || span.begin >= span.end ||
            span.begin >= static_cast<std::uint64_t>(rawLength)) {
            continue;
        }
        const auto end64 = std::min<std::uint64_t>(span.end,
            static_cast<std::uint64_t>(rawLength));
        if (end64 > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            continue;
        }
        const auto begin = static_cast<int>(span.begin);
        const auto length = static_cast<int>(end64 - span.begin);
        if (length > 0) {
            control_->IndicatorFillRange(begin, length);
        }
    }
    if (revealedSpoiler_) {
        const auto begin64 = std::min<std::uint64_t>(revealedSpoiler_->begin,
            static_cast<std::uint64_t>(rawLength));
        const auto end64 = std::min<std::uint64_t>(revealedSpoiler_->end,
            static_cast<std::uint64_t>(rawLength));
        if (begin64 < end64 && end64 <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            control_->IndicatorClearRange(static_cast<int>(begin64),
                                          static_cast<int>(end64 - begin64));
        }
    }
}

void WxEditorHost::updateSpoilerRevealAt(const int position) {
    if (control_ == nullptr || !hasSpoilers_) {
        return;
    }
    std::optional<AppearanceRange> next;
    if (position >= 0) {
        next = contiguousSpoilerRegionAt(
            appearanceSpans_, static_cast<std::uint64_t>(position));
    }
    if (next == revealedSpoiler_) {
        return;
    }

    control_->SetIndicatorCurrent(spoilerIndicator);
    const auto rawLength = static_cast<std::uint64_t>(std::max(control_->GetLength(), 0));
    if (revealedSpoiler_) {
        const auto begin = std::min(revealedSpoiler_->begin, rawLength);
        const auto end = std::min(revealedSpoiler_->end, rawLength);
        if (begin < end && end <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            control_->IndicatorFillRange(static_cast<int>(begin), static_cast<int>(end - begin));
        }
    }
    if (next) {
        const auto begin = std::min(next->begin, rawLength);
        const auto end = std::min(next->end, rawLength);
        if (begin < end && end <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            control_->IndicatorClearRange(static_cast<int>(begin), static_cast<int>(end - begin));
        }
    }
    revealedSpoiler_ = next;
    control_->Refresh(false);
}

void WxEditorHost::clearLinkIndicators() {
    if (control_ == nullptr) {
        return;
    }
    if (linkIndicatorLength_ > 0) {
        const int documentLength = std::max(control_->GetLength(), 0);
        const int start = std::clamp(linkIndicatorStart_, 0, documentLength);
        const int length = std::min(linkIndicatorLength_, documentLength - start);
        if (length > 0) {
            control_->SetIndicatorCurrent(linkIndicator);
            control_->IndicatorClearRange(start, length);
        }
    }
    visibleLinks_.clear();
    linkIndicatorStart_ = 0;
    linkIndicatorLength_ = 0;
    linkFirstVisibleLine_ = -1;
    linkLastVisibleLine_ = -1;
}

void WxEditorHost::refreshLinkIndicators(const bool force) {
    if (control_ == nullptr || settings_ == nullptr) {
        return;
    }
    if (!settings_->highlightUrls || mode_ == core::OpenMode::BinaryPreview) {
        if (linkIndicatorLength_ > 0 || !visibleLinks_.empty()) {
            clearLinkIndicators();
        }
        forceLinkRefresh_ = true;
        return;
    }

    const int lineCount = std::max(control_->GetLineCount(), 1);
    const int firstDisplayLine = std::max(control_->GetFirstVisibleLine(), 0);
    const int visibleCount = std::max(control_->LinesOnScreen(), 1);
    const int lastDisplayLine = firstDisplayLine + visibleCount + 2;
    const int firstVisible = std::clamp(control_->DocLineFromVisible(firstDisplayLine), 0,
                                        lineCount - 1);
    const int lastVisible = std::clamp(control_->DocLineFromVisible(lastDisplayLine), firstVisible,
                                       lineCount - 1);
    if (!force && !forceLinkRefresh_ && linkRevision_ == documentRevision_ &&
        linkFirstVisibleLine_ == firstVisible && linkLastVisibleLine_ == lastVisible) {
        return;
    }

    const int start = std::max(control_->PositionFromLine(firstVisible), 0);
    int end = control_->GetLineEndPosition(lastVisible);
    if (lastVisible + 1 < lineCount) {
        end = std::max(end, control_->PositionFromLine(lastVisible + 1));
    }
    end = std::clamp(end, start, std::max(control_->GetLength(), 0));

    clearLinkIndicators();
    linkFirstVisibleLine_ = firstVisible;
    linkLastVisibleLine_ = lastVisible;
    linkRevision_ = documentRevision_;
    forceLinkRefresh_ = false;
    if (end <= start) {
        return;
    }

    const auto slice = utf8(control_->GetTextRange(start, end));
    auto detected = core::LinkDetector::detect(slice, 512U);
    visibleLinks_.reserve(detected.size());
    control_->SetIndicatorCurrent(linkIndicator);
    for (auto& link : detected) {
        if (link.offset > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
            link.length > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            continue;
        }
        const auto absolute = static_cast<std::uint64_t>(start) +
                              static_cast<std::uint64_t>(link.offset);
        const auto absoluteEnd = absolute + static_cast<std::uint64_t>(link.length);
        if (absoluteEnd > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            continue;
        }
        link.offset = static_cast<std::size_t>(absolute);
        control_->IndicatorFillRange(static_cast<int>(absolute), static_cast<int>(link.length));
        visibleLinks_.push_back(std::move(link));
    }
    linkIndicatorStart_ = start;
    linkIndicatorLength_ = end - start;
}

void WxEditorHost::updateLineNumberMargin(const bool visible) {
    if (control_ == nullptr) {
        return;
    }

    int width = 0;
    if (visible) {
        auto lines = std::max(control_->GetLineCount(), 1);
        std::size_t digits = 1U;
        while (lines >= 10) {
            lines /= 10;
            ++digits;
        }
        const std::string sample(digits, '9');
        const auto textWidth = control_->TextWidth(
            wxSTC_STYLE_LINENUMBER, wxString::FromUTF8(sample.c_str(), sample.size()));
        const int padding = settings_ != nullptr &&
                                    settings_->density == settings::UiDensity::Compact
                                ? 7
                                : 9;
        width = std::max(textWidth + padding, 12);
    }

    if (width != lineNumberMarginWidth_) {
        control_->SetMarginWidth(0, width);
        lineNumberMarginWidth_ = width;
    }
}

void WxEditorHost::reloadText(const EditorHostBinding& binding) {
    if (control_ == nullptr) {
        return;
    }

    const auto caret = control_->GetCurrentPos();
    const auto anchor = control_->GetAnchor();
    const auto firstVisible = control_->GetFirstVisibleLine();
    const bool readOnly = control_->GetReadOnly();

    suppressModified_ = true;
    if (readOnly) {
        control_->SetReadOnly(false);
    }
    control_->SetText(wxString::FromUTF8(binding.text.data(), binding.text.size()));
    control_->EmptyUndoBuffer();
    appearanceUndoJournal_.clear();
    control_->SetSavePoint();
    if (readOnly) {
        control_->SetReadOnly(true);
    }
    suppressModified_ = false;

    const auto length = std::max(control_->GetLength(), 0);
    control_->SetSelection(std::clamp(anchor, 0, length), std::clamp(caret, 0, length));
    control_->ScrollToLine(std::max(firstVisible, 0));

    document_ = binding.document;
    documentRevision_ = binding.documentRevision;
    path_ = binding.path;
    forceReload_ = false;
}

WxEditorHostFactory::WxEditorHostFactory(wxWindow& parent,
                                         const WxThemePalette& theme,
                                         const settings::AppSettings& settings,
                                         SynchronizeRequest synchronizeRequest,
                                         ActivateRequest activateRequest,
                                         ContextMenuRequest contextMenuRequest,
                                         LinkActivateRequest linkActivateRequest,
                                         FileDropRequest fileDropRequest)
    : parent_(&parent),
      theme_(&theme),
      settings_(&settings),
      synchronizeRequest_(std::move(synchronizeRequest)),
      activateRequest_(std::move(activateRequest)),
      contextMenuRequest_(std::move(contextMenuRequest)),
      linkActivateRequest_(std::move(linkActivateRequest)),
      fileDropRequest_(std::move(fileDropRequest)) {}

std::unique_ptr<IEditorHost> WxEditorHostFactory::create(
    const workspace::ViewId view,
    IEditorHostEvents& events) {
    if (parent_ == nullptr || theme_ == nullptr || settings_ == nullptr) {
        return {};
    }
    return std::make_unique<WxEditorHost>(
        *parent_, view, events, *theme_, *settings_, synchronizeRequest_, activateRequest_,
        contextMenuRequest_, linkActivateRequest_, fileDropRequest_);
}

}
