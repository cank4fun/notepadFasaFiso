#include "notepadFasaFiso/gui/GuiRuntime.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace nff::gui {
namespace {

[[nodiscard]] bool sameRuntime(const app::ViewRuntimeState& left,
                               const app::ViewRuntimeState& right) noexcept {
    return left.openMode == right.openMode &&
           left.viewerPerformance == right.viewerPerformance &&
           left.resolvedViewerPerformance == right.resolvedViewerPerformance &&
           left.initialized == right.initialized &&
           left.followEnabled == right.followEnabled &&
           left.followWaiting == right.followWaiting &&
           left.hasSelection == right.hasSelection &&
           left.canUndo == right.canUndo &&
           left.canRedo == right.canRedo &&
           left.canPaste == right.canPaste &&
           left.wordWrap == right.wordWrap &&
           left.lineNumbersVisible == right.lineNumbersVisible &&
           left.fontFamily == right.fontFamily &&
           left.fontPointSize == right.fontPointSize &&
           left.caretLine == right.caretLine &&
           left.caretColumn == right.caretColumn &&
           left.selectionBytes == right.selectionBytes &&
           left.contentBytes == right.contentBytes &&
           left.windowByteStart == right.windowByteStart &&
           left.windowByteEnd == right.windowByteEnd &&
           left.viewerCacheResidentBytes == right.viewerCacheResidentBytes;
}

}

GuiRuntime::GuiRuntime(core::DocumentManager& documents,
                       workspace::WorkspaceModel& workspace,
                       app::PresentationModel& presentation,
                       GuiShell& shell,
                       IEditorHostFactory& hostFactory,
                       const GuiRuntimePolicy policy,
                       GuiRuntimeCallbacks callbacks) noexcept
    : documents_(&documents),
      workspace_(&workspace),
      presentation_(&presentation),
      shell_(&shell),
      hostFactory_(&hostFactory),
      policy_(policy),
      callbacks_(std::move(callbacks)) {}

void GuiRuntime::setPolicy(const GuiRuntimePolicy policy) noexcept {
    policy_ = policy;
    pruneDormantHosts();
}

GuiRuntimePolicy GuiRuntime::policy() const noexcept {
    return policy_;
}

const GuiFrame& GuiRuntime::synchronize() {
    ++generation_;
    const auto& frame = shell_->refresh();

    for (auto& [view, entry] : hosts_) {
        static_cast<void>(view);
        entry.visible = false;
    }
    synchronizeActiveHosts(frame);
    hideDormantHosts();
    pruneDormantHosts();

    bool presentationChanged = false;
    for (auto& [view, entry] : hosts_) {
        if (entry.visible) {
            presentationChanged = harvestRuntime(view, *entry.host) || presentationChanged;
        }
    }

    if (presentationChanged) {
        shell_->requestRefresh();
        return shell_->refresh();
    }
    return frame;
}

IEditorHost* GuiRuntime::host(const workspace::ViewId view) noexcept {
    const auto iterator = hosts_.find(view);
    return iterator == hosts_.end() ? nullptr : iterator->second.host.get();
}

const IEditorHost* GuiRuntime::host(const workspace::ViewId view) const noexcept {
    const auto iterator = hosts_.find(view);
    return iterator == hosts_.end() ? nullptr : iterator->second.host.get();
}

GuiRuntimeStats GuiRuntime::stats() const noexcept {
    GuiRuntimeStats result;
    result.generation = generation_;
    result.residentHosts = hosts_.size();
    for (const auto& [view, entry] : hosts_) {
        static_cast<void>(view);
        if (entry.visible) {
            ++result.visibleHosts;
        }
    }
    result.dormantHosts = result.residentHosts - result.visibleHosts;
    return result;
}

bool GuiRuntime::executeEditorCommand(const workspace::ViewId view,
                                      const EditorHostCommand command) {
    auto* entry = ensureHost(view);
    if (entry == nullptr || !entry->host->execute(command)) {
        return false;
    }
    shell_->requestRefresh();
    return true;
}

EditorHostSearchResult GuiRuntime::findText(const workspace::ViewId viewId,
                                            const EditorHostSearchRequest& request) {
    EditorHostSearchResult result;
    if (request.pattern.empty()) {
        result.error = std::make_error_code(std::errc::invalid_argument);
        return result;
    }

    const auto* viewState = workspace_->view(viewId);
    auto* entry = ensureHost(viewId);
    if (viewState == nullptr || entry == nullptr) {
        result.error = std::make_error_code(std::errc::invalid_argument);
        return result;
    }

    const auto binding = bindingFor(viewId);
    entry->host->bind(binding);
    if (binding.mode == core::OpenMode::BinaryPreview) {
        result.error = std::make_error_code(std::errc::operation_not_supported);
        return result;
    }

    if (binding.mode == core::OpenMode::Editor) {
        const auto* document = documents_->get(viewState->document);
        if (document == nullptr || !document->textBufferLoaded()) {
            result.error = std::make_error_code(std::errc::operation_not_supported);
            return result;
        }

        const auto text = document->text();
        const auto textSize = static_cast<std::uint64_t>(text.size());
        const auto start64 = std::min(request.startOffset, textSize);
        const auto found = search::TextSearch::find(
            text,
            request.pattern,
            static_cast<std::size_t>(start64),
            request.direction,
            request.options);
        result.error = found.error;
        result.match = found.match;
        if (!result || !result.match) {
            return result;
        }
        result.wrapped = request.direction == search::SearchDirection::Forward
                             ? result.match->offset < start64
                             : result.match->offset >= start64;
    } else {
        result = entry->host->findText(request);
        if (!result || !result.match) {
            return result;
        }
    }

    if (!entry->host->revealTextMatch(*result.match, request.pattern)) {
        result.error = std::make_error_code(std::errc::operation_not_supported);
        return result;
    }
    shell_->requestRefresh();
    return result;
}

bool GuiRuntime::replaceTextRange(
    const workspace::ViewId viewId,
    const search::SearchMatch match,
    const std::string_view replacement,
    const metadata::TextColorEditPolicy colorPolicy) {
    const auto* viewState = workspace_->view(viewId);
    auto* entry = ensureHost(viewId);
    if (viewState == nullptr || entry == nullptr) {
        return false;
    }

    const auto binding = bindingFor(viewId);
    if (binding.mode != core::OpenMode::Editor || !binding.textBufferLoaded) {
        return false;
    }
    entry->host->bind(binding);
    if (!entry->host->replaceTextRange(match, replacement, colorPolicy)) {
        return false;
    }
    shell_->requestRefresh();
    return true;
}

bool GuiRuntime::replaceAllText(
    const workspace::ViewId viewId,
    const std::string_view text,
    const metadata::TextColorEditPolicy colorPolicy) {
    const auto* viewState = workspace_->view(viewId);
    auto* entry = ensureHost(viewId);
    if (viewState == nullptr || entry == nullptr) {
        return false;
    }

    const auto binding = bindingFor(viewId);
    if (binding.mode != core::OpenMode::Editor || !binding.textBufferLoaded) {
        return false;
    }
    entry->host->bind(binding);
    if (!entry->host->replaceAllText(text, colorPolicy)) {
        return false;
    }
    shell_->requestRefresh();
    return true;
}

std::error_code GuiRuntime::goToLine(const workspace::ViewId viewId,
                                     const std::uint64_t oneBasedLine) {
    if (oneBasedLine == 0U) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    auto* entry = ensureHost(viewId);
    if (entry == nullptr || workspace_->view(viewId) == nullptr) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    entry->host->bind(bindingFor(viewId));
    const auto error = entry->host->goToLine(oneBasedLine);
    if (!error) {
        shell_->requestRefresh();
    }
    return error;
}

std::error_code GuiRuntime::goToPosition(const workspace::ViewId viewId,
                                         const std::uint64_t oneBasedLine,
                                         const std::uint64_t oneBasedColumn) {
    if (oneBasedLine == 0U || oneBasedColumn == 0U) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    auto* entry = ensureHost(viewId);
    if (entry == nullptr || workspace_->view(viewId) == nullptr) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    entry->host->bind(bindingFor(viewId));
    const auto error = entry->host->goToPosition(oneBasedLine, oneBasedColumn);
    if (!error) {
        shell_->requestRefresh();
    }
    return error;
}

void GuiRuntime::refreshHostAppearance() {
    for (auto& [view, entry] : hosts_) {
        static_cast<void>(view);
        entry.host->refreshAppearance();
    }
}

GuiRuntimePollResult GuiRuntime::pollLiveContent() {
    GuiRuntimePollResult aggregate;
    bool presentationChanged = false;
    for (auto& [view, entry] : hosts_) {
        const auto polled = entry.host->pollLiveContent();
        if (polled.error && !aggregate.error) {
            aggregate.error = polled.error;
        }
        if (!polled.changed) {
            continue;
        }
        aggregate.changed = true;
        ++aggregate.changedHosts;
        presentationChanged = harvestRuntime(view, *entry.host) || presentationChanged;
    }
    if (aggregate.changed || presentationChanged) {
        shell_->requestRefresh();
    }
    return aggregate;
}

void GuiRuntime::invalidateDocument(const core::DocumentId document) noexcept {
    for (auto iterator = hosts_.begin(); iterator != hosts_.end();) {
        const auto* view = workspace_->view(iterator->first);
        if (view != nullptr && view->document == document) {
            iterator = hosts_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    shell_->requestRefresh();
}

bool GuiRuntime::applyTextEdit(const workspace::ViewId viewId,
                               const EditorTextEdit& edit) {
    const auto* viewState = workspace_->view(viewId);
    if (viewState == nullptr) {
        return false;
    }
    auto* document = documents_->get(viewState->document);
    if (document == nullptr || !document->textBufferLoaded() ||
        document->revision() != edit.baseRevision) {
        return false;
    }
    if (const auto* runtime = presentation_->viewRuntime(viewId);
        runtime != nullptr && runtime->openMode != core::OpenMode::Editor) {
        return false;
    }

    if (callbacks_.documentWillEdit) {
        callbacks_.documentWillEdit(viewState->document);
    }

    const auto error = document->applyEdit(edit.offset, edit.eraseBytes, edit.insertedText);
    if (error) {
        return false;
    }
    if (callbacks_.documentEdited) {
        callbacks_.documentEdited(viewState->document, edit);
    }
    shell_->requestRefresh();
    return true;
}

bool GuiRuntime::restoreTextAppearanceRange(
    const workspace::ViewId viewId,
    const std::uint64_t begin,
    const std::uint64_t end,
    const std::span<const metadata::TextAppearanceSpan> spans) {
    const auto* viewState = workspace_->view(viewId);
    if (viewState == nullptr || !callbacks_.appearanceRangeRestored) {
        return false;
    }
    if (!callbacks_.appearanceRangeRestored(viewState->document, begin, end, spans)) {
        return false;
    }
    shell_->requestRefresh();
    return true;
}

GuiRuntime::HostEntry* GuiRuntime::ensureHost(const workspace::ViewId view) {
    auto iterator = hosts_.find(view);
    if (iterator != hosts_.end()) {
        return &iterator->second;
    }

    auto created = hostFactory_->create(view, *this);
    if (!created) {
        return nullptr;
    }
    created->restoreViewState(persistedViewState(view));
    auto [inserted, success] = hosts_.emplace(
        view, HostEntry{std::move(created), generation_, false});
    if (!success) {
        return nullptr;
    }
    return &inserted->second;
}

void GuiRuntime::synchronizeActiveHosts(const GuiFrame& frame) {
    for (const auto& pane : frame.presentation.panes) {
        if (!pane.activeView) {
            continue;
        }
        const auto* paneLayout = layoutForPane(frame.layout, pane.pane);
        if (paneLayout == nullptr) {
            continue;
        }

        const auto view = *pane.activeView;
        auto* entry = ensureHost(view);
        if (entry == nullptr) {
            continue;
        }

        entry->visible = true;
        entry->lastActiveGeneration = generation_;
        entry->host->bind(bindingFor(view));
        entry->host->setBounds(paneLayout->content);
        entry->host->setVisible(true);
        entry->host->setFocused(frame.presentation.activeView &&
                                *frame.presentation.activeView == view);
    }
}

void GuiRuntime::hideDormantHosts() {
    for (auto& [view, entry] : hosts_) {
        static_cast<void>(view);
        if (!entry.visible) {
            entry.host->setVisible(false);
            entry.host->setFocused(false);
        }
    }
}

void GuiRuntime::pruneDormantHosts() {
    const auto dormantCount = static_cast<std::size_t>(std::count_if(
        hosts_.begin(), hosts_.end(), [](const auto& item) { return !item.second.visible; }));
    if (dormantCount <= policy_.maximumDormantHosts) {
        return;
    }

    std::vector<std::pair<workspace::ViewId, std::uint64_t>> dormant;
    dormant.reserve(dormantCount);
    for (const auto& [view, entry] : hosts_) {
        if (!entry.visible) {
            dormant.emplace_back(view, entry.lastActiveGeneration);
        }
    }
    std::sort(dormant.begin(), dormant.end(), [](const auto& left, const auto& right) {
        if (left.second != right.second) {
            return left.second < right.second;
        }
        return left.first.value < right.first.value;
    });

    const auto removeCount = dormant.size() - policy_.maximumDormantHosts;
    for (std::size_t index = 0U; index < removeCount; ++index) {
        hosts_.erase(dormant[index].first);
    }
}

bool GuiRuntime::harvestRuntime(const workspace::ViewId viewId, IEditorHost& editorHost) {
    const auto value = editorHost.runtime();
    auto* persisted = workspace_->view(viewId);
    if (persisted != nullptr &&
        (persisted->caretOffset != value.caretOffset ||
         persisted->anchorOffset != value.anchorOffset ||
         persisted->firstVisibleLine != value.firstVisibleLine)) {
        persisted->caretOffset = value.caretOffset;
        persisted->anchorOffset = value.anchorOffset;
        persisted->firstVisibleLine = value.firstVisibleLine;
    }

    app::ViewRuntimeState next;
    if (const auto* current = presentation_->viewRuntime(viewId)) {
        next = *current;
    } else {
        const auto binding = bindingFor(viewId);
        next.openMode = binding.mode;
        next.viewerPerformance = binding.viewerPerformance;
        next.resolvedViewerPerformance = binding.viewerPerformance;
        next.wordWrap = binding.wordWrap;
        next.lineNumbersVisible = binding.lineNumbersVisible;
        next.fontFamily = std::string(binding.fontFamily);
        next.fontPointSize = binding.fontPointSize;
    }
    next.initialized = true;
    next.caretLine = std::max<std::uint64_t>(value.caretLine, 1U);
    next.caretColumn = std::max<std::uint64_t>(value.caretColumn, 1U);
    next.selectionBytes = value.selectionBytes;
    next.contentBytes = value.contentBytes;
    next.windowByteStart = value.windowByteStart;
    next.windowByteEnd = value.windowByteEnd;
    next.viewerCacheResidentBytes = value.viewerCacheResidentBytes;
    next.resolvedViewerPerformance = value.resolvedViewerPerformance;
    next.hasSelection = value.hasSelection;
    next.canUndo = value.canUndo;
    next.canRedo = value.canRedo;
    next.canPaste = value.canPaste;
    next.followWaiting = value.followWaiting;

    const auto* current = presentation_->viewRuntime(viewId);
    const bool changed = current == nullptr || !sameRuntime(*current, next);
    if (changed) {
        static_cast<void>(presentation_->setViewRuntime(viewId, next));
    }
    return changed;
}

const PaneLayout* GuiRuntime::layoutForPane(const ShellLayout& layout,
                                            const workspace::PaneId pane) const noexcept {
    const auto iterator = std::find_if(layout.panes.begin(), layout.panes.end(),
                                       [pane](const PaneLayout& value) {
                                           return value.pane == pane;
                                       });
    return iterator == layout.panes.end() ? nullptr : &*iterator;
}

EditorHostBinding GuiRuntime::bindingFor(const workspace::ViewId viewId) const {
    EditorHostBinding result;
    result.view = viewId;

    const auto* view = workspace_->view(viewId);
    if (view == nullptr) {
        return result;
    }
    result.document = view->document;
    result.preferredByteOffset = view->caretOffset;

    const auto* document = documents_->get(view->document);
    if (document != nullptr) {
        result.path = document->path();
        result.documentProfile = &document->profile();
        result.documentFileState = document->trackedFileState();
        result.text = document->text();
        result.textBufferLoaded = document->textBufferLoaded();
        result.documentRevision = document->revision();
    }

    if (const auto* runtime = presentation_->viewRuntime(viewId)) {
        result.mode = runtime->openMode;
        result.viewerPerformance = runtime->viewerPerformance;
        result.wordWrap = runtime->wordWrap;
        result.lineNumbersVisible = runtime->lineNumbersVisible;
        result.followEnabled = runtime->followEnabled;
        result.fontFamily = runtime->fontFamily;
        result.fontPointSize = runtime->fontPointSize;
    }
    if (callbacks_.appearance) {
        const auto appearance = callbacks_.appearance(view->document);
        result.appearanceSpans = appearance.spans;
        result.appearanceFontFamilies = appearance.fontFamilies;
        result.appearanceRevision = appearance.revision;
        result.hasSpoilers = appearance.hasSpoilers;
    }
    return result;
}

EditorHostViewState GuiRuntime::persistedViewState(const workspace::ViewId viewId) const noexcept {
    EditorHostViewState result;
    const auto* view = workspace_->view(viewId);
    if (view != nullptr) {
        result.caretOffset = view->caretOffset;
        result.anchorOffset = view->anchorOffset;
        result.firstVisibleLine = view->firstVisibleLine;
    }
    return result;
}

}
