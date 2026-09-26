#include "notepadFasaFiso/app/PresentationModel.hpp"

#include "notepadFasaFiso/encoding/EncodingDetector.hpp"
#include "notepadFasaFiso/formats/FormatDetector.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace nff::app {
namespace {

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::string lineEndingName(const core::LineEnding lineEnding) {
    switch (lineEnding) {
    case core::LineEnding::LF:
        return "LF";
    case core::LineEnding::CRLF:
        return "CRLF";
    case core::LineEnding::CR:
        return "CR";
    case core::LineEnding::Mixed:
        return "Mixed";
    case core::LineEnding::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

[[nodiscard]] std::string openModeName(const core::OpenMode mode) {
    switch (mode) {
    case core::OpenMode::Editor:
        return "Editor";
    case core::OpenMode::Viewer:
        return "View";
    case core::OpenMode::BinaryPreview:
        return "Hex";
    }
    return "Unknown";
}

[[nodiscard]] std::string performanceName(const viewer::PerformanceProfile profile) {
    switch (profile) {
    case viewer::PerformanceProfile::Automatic:
        return "Auto";
    case viewer::PerformanceProfile::Fast:
        return "Fast";
    case viewer::PerformanceProfile::MemorySaver:
        return "Memory Saver";
    }
    return "Auto";
}

[[nodiscard]] std::string untitledTitle(const core::DocumentId id) {
    return "Untitled " + std::to_string(id.value);
}

}

PresentationModel::PresentationModel(const core::DocumentManager& documents,
                                     const workspace::WorkspaceModel& workspace,
                                     const settings::AppSettings& settings) noexcept
    : documents_(&documents),
      workspace_(&workspace),
      settings_(&settings) {
    sidebar_.visible = settings.sidebarVisible;
    sync();
}

void PresentationModel::sync() {
    workspace_->forEachView([this](const workspace::ViewState& view) {
        if (!viewRuntime_.contains(view.id)) {
            viewRuntime_.emplace(view.id, defaultRuntime(view.id));
        }
    });
    std::erase_if(viewRuntime_, [this](const auto& item) {
        return workspace_->view(item.first) == nullptr;
    });
    std::erase_if(externalChanges_, [this](const auto& item) {
        return documents_->get(item.first) == nullptr;
    });
    std::erase_if(recoveredDocuments_, [this](const auto& item) {
        return documents_->get(item.first) == nullptr;
    });
}

bool PresentationModel::setViewRuntime(const workspace::ViewId view,
                                       const ViewRuntimeState& state) {
    if (workspace_->view(view) == nullptr) {
        return false;
    }

    auto normalized = state;
    normalized.initialized = true;
    normalized.caretLine = std::max<std::uint64_t>(normalized.caretLine, 1U);
    normalized.caretColumn = std::max<std::uint64_t>(normalized.caretColumn, 1U);
    viewRuntime_[view] = normalized;
    return true;
}

const ViewRuntimeState* PresentationModel::viewRuntime(const workspace::ViewId view) const noexcept {
    const auto iterator = viewRuntime_.find(view);
    return iterator == viewRuntime_.end() ? nullptr : &iterator->second;
}

bool PresentationModel::eraseViewRuntime(const workspace::ViewId view) noexcept {
    return viewRuntime_.erase(view) != 0U;
}

void PresentationModel::setExternalConflict(const core::DocumentId document,
                                            const bool conflict) noexcept {
    setExternalChangeState(document, conflict ? storage::FileChangeState::Modified
                                              : storage::FileChangeState::Unchanged);
}

void PresentationModel::setExternalChangeState(const core::DocumentId document,
                                               const storage::FileChangeState state) noexcept {
    if (!document) {
        return;
    }
    if (state == storage::FileChangeState::Modified ||
        state == storage::FileChangeState::Deleted ||
        state == storage::FileChangeState::Inaccessible) {
        externalChanges_[document] = state;
    } else {
        externalChanges_.erase(document);
    }
}

bool PresentationModel::externalConflict(const core::DocumentId document) const noexcept {
    return externalChangeState(document) != storage::FileChangeState::Untracked;
}

storage::FileChangeState PresentationModel::externalChangeState(
    const core::DocumentId document) const noexcept {
    const auto iterator = externalChanges_.find(document);
    return iterator == externalChanges_.end() ? storage::FileChangeState::Untracked
                                              : iterator->second;
}

void PresentationModel::setRecovered(const core::DocumentId document,
                                     const bool recoveredValue) noexcept {
    if (!document) {
        return;
    }
    if (recoveredValue) {
        recoveredDocuments_[document] = true;
    } else {
        recoveredDocuments_.erase(document);
    }
}

bool PresentationModel::recovered(const core::DocumentId document) const noexcept {
    const auto iterator = recoveredDocuments_.find(document);
    return iterator != recoveredDocuments_.end() && iterator->second;
}

void PresentationModel::setSidebarVisible(const bool visible) noexcept {
    sidebar_.visible = visible;
}

void PresentationModel::setSidebarWidth(const double width) noexcept {
    if (!std::isfinite(width)) {
        return;
    }
    sidebar_.width = std::clamp(width, minimumSidebarWidth, maximumSidebarWidth);
}

void PresentationModel::setSidebarFolderActive(const bool active) noexcept {
    sidebar_.folderActive = active;
}

CommandContext PresentationModel::commandContext() const {
    CommandContext context;
    context.sidebarVisible = sidebar_.visible;
    context.sidebarFolderActive = sidebar_.folderActive;
    context.tabsEnabled = settings_->tabsEnabled;
    context.wordWrap = settings_->wordWrap;
    context.lineNumbersVisible = settings_->showLineNumbers;
    context.linkDetectionEnabled = settings_->highlightUrls;
    context.viewerPerformance = settings_->viewerPerformance;
    context.paneCount = workspace_->paneCount();

    const auto viewId = activeView();
    if (!viewId) {
        return context;
    }

    const auto* view = workspace_->view(*viewId);
    const auto* document = documentFor(*viewId);
    if (view == nullptr || document == nullptr) {
        return context;
    }

    const auto& runtime = runtimeFor(*viewId);
    const auto format = document->profile().format.format;
    const auto capabilities = formats::FormatDetector::descriptor(format).capabilities;

    context.hasDocument = true;
    context.hasView = true;
    context.hasBackingFile = !document->path().empty();
    context.openMode = runtime.openMode;
    context.editable = runtime.openMode == core::OpenMode::Editor && document->textBufferLoaded();
    context.modified = document->modified();
    context.textBufferLoaded = document->textBufferLoaded();
    context.hasSelection = runtime.hasSelection;
    context.canUndo = runtime.canUndo;
    context.canRedo = runtime.canRedo;
    context.canPaste = runtime.canPaste;
    context.wordWrap = runtime.wordWrap;
    context.lineNumbersVisible = runtime.lineNumbersVisible;
    context.followAvailable = context.hasBackingFile && runtime.openMode == core::OpenMode::Viewer;
    context.followEnabled = runtime.followEnabled;
    context.viewerPerformance = runtime.viewerPerformance;
    context.saveEncoding = document->saveEncoding();
    context.writesBom = document->writesBom();
    context.lineEnding = document->profile().lineEnding;
    context.formatCanValidate = format == formats::TextFormat::Json && capabilities.validation;
    context.formatCanPrettyPrint = format == formats::TextFormat::Json && capabilities.prettyPrint;
    context.formatCanMinify = format == formats::TextFormat::Json && capabilities.minify;
    return context;
}

ApplicationPresentationSnapshot PresentationModel::snapshot() const {
    ApplicationPresentationSnapshot result;
    result.tabsVisible = settings_->tabsEnabled;
    result.activePane = workspace_->activePane();
    result.activeView = activeView();
    if (result.activeView) {
        const auto* view = workspace_->view(*result.activeView);
        if (view != nullptr) {
            result.activeDocument = view->document;
        }
    }

    result.panes.reserve(workspace_->paneCount());
    workspace_->forEachPane([this, &result](const workspace::PaneState& pane) {
        result.panes.push_back(buildPane(pane));
    });
    result.splitRoot = buildSplitNode(workspace_->root());
    result.sidebar = sidebar_;
    result.status = buildStatus();
    result.commands = buildCommands();
    return result;
}

std::optional<workspace::ViewId> PresentationModel::activeView() const noexcept {
    const auto* pane = workspace_->pane(workspace_->activePane());
    if (pane == nullptr || !pane->activeView) {
        return std::nullopt;
    }
    return pane->activeView;
}

const core::Document* PresentationModel::documentFor(const workspace::ViewId viewId) const noexcept {
    const auto* view = workspace_->view(viewId);
    if (view == nullptr) {
        return nullptr;
    }
    return documents_->get(view->document);
}

ViewRuntimeState PresentationModel::defaultRuntime(const workspace::ViewId viewId) const noexcept {
    ViewRuntimeState result;
    result.initialized = true;
    result.viewerPerformance = settings_->viewerPerformance;
    result.resolvedViewerPerformance = settings_->viewerPerformance;
    result.wordWrap = settings_->wordWrap;
    result.lineNumbersVisible = settings_->showLineNumbers;
    result.fontFamily = settings_->fontFamily;
    result.fontPointSize = settings_->fontPointSize;

    const auto* view = workspace_->view(viewId);
    if (view != nullptr) {
        result.wordWrap = view->wordWrapOverride.value_or(result.wordWrap);
        result.lineNumbersVisible = view->lineNumbersOverride.value_or(result.lineNumbersVisible);
        result.fontFamily = view->fontFamilyOverride.value_or(result.fontFamily);
        result.fontPointSize = view->fontPointSizeOverride.value_or(result.fontPointSize);
    }

    const auto* document = documentFor(viewId);
    if (document != nullptr) {
        result.openMode = document->profile().recommendedMode;
    }
    return result;
}

const ViewRuntimeState& PresentationModel::runtimeFor(const workspace::ViewId viewId) const {
    const auto iterator = viewRuntime_.find(viewId);
    if (iterator != viewRuntime_.end()) {
        return iterator->second;
    }

    static const ViewRuntimeState fallback{};
    return fallback;
}

std::unique_ptr<SplitPresentationNode> PresentationModel::buildSplitNode(
    const workspace::WorkspaceNode& node) const {
    auto result = std::make_unique<SplitPresentationNode>();
    result->kind = node.kind();
    result->pane = node.pane();
    result->split = node.split();
    result->orientation = node.orientation();
    result->ratio = node.ratio();
    if (node.first() != nullptr) {
        result->first = buildSplitNode(*node.first());
    }
    if (node.second() != nullptr) {
        result->second = buildSplitNode(*node.second());
    }
    return result;
}

PanePresentation PresentationModel::buildPane(const workspace::PaneState& pane) const {
    PanePresentation result;
    result.pane = pane.id;
    result.activeView = pane.activeView;
    result.active = pane.id == workspace_->activePane();
    result.tabs.reserve(pane.views.size());
    for (const auto view : pane.views) {
        result.tabs.push_back(buildTab(view, pane.activeView && *pane.activeView == view));
    }
    return result;
}

TabPresentation PresentationModel::buildTab(const workspace::ViewId viewId,
                                             const bool active) const {
    TabPresentation result;
    result.view = viewId;
    result.active = active;

    const auto* view = workspace_->view(viewId);
    if (view == nullptr) {
        result.title = "Missing view";
        return result;
    }

    result.document = view->document;
    const auto* document = documents_->get(view->document);
    if (document == nullptr) {
        result.title = "Missing document";
        return result;
    }

    result.modified = document->modified();
    result.externalConflict = document->requiresExplicitOverwrite() ||
                              externalConflict(view->document);
    result.recovered = recovered(view->document);
    result.untitled = document->path().empty();
    if (result.untitled) {
        result.title = untitledTitle(view->document);
    } else {
        result.title = pathToUtf8(document->path().filename());
        result.path = pathToUtf8(document->path());
    }
    return result;
}

StatusBarPresentation PresentationModel::buildStatus() const {
    StatusBarPresentation result;
    const auto viewId = activeView();
    if (!viewId) {
        return result;
    }

    const auto* document = documentFor(*viewId);
    if (document == nullptr) {
        return result;
    }

    const auto& runtime = runtimeFor(*viewId);
    result.visible = true;
    result.line = runtime.caretLine;
    result.column = runtime.caretColumn;
    result.selectionBytes = runtime.selectionBytes;
    const auto displayEncoding = document->textBufferLoaded()
                                     ? document->saveEncoding()
                                     : document->profile().encoding.encoding;
    result.encoding = std::string(encoding::EncodingDetector::name(displayEncoding));
    result.writesBom = document->textBufferLoaded()
                           ? document->writesBom()
                           : document->profile().encoding.hasBom;
    result.lineEnding = lineEndingName(document->profile().lineEnding);
    result.format = std::string(formats::FormatDetector::descriptor(
                                    document->profile().format.format)
                                    .name);
    result.mode = openModeName(runtime.openMode);
    if (runtime.openMode == core::OpenMode::Viewer) {
        result.viewerPerformance = performanceName(runtime.viewerPerformance);
        result.viewerResolvedPerformance = performanceName(runtime.resolvedViewerPerformance);
        result.followEnabled = runtime.followEnabled;
        result.followWaiting = runtime.followWaiting;
    }
    result.contentBytes = runtime.contentBytes != 0U
                              ? runtime.contentBytes
                              : static_cast<std::uint64_t>(document->profile().fileSize);
    result.windowByteStart = runtime.windowByteStart;
    result.windowByteEnd = runtime.windowByteEnd;
    result.viewerCacheResidentBytes = runtime.viewerCacheResidentBytes;
    result.modified = document->modified();
    result.explicitOverwriteRequired = document->requiresExplicitOverwrite();
    const auto* view = workspace_->view(*viewId);
    if (view != nullptr) {
        result.externalChangeState = externalChangeState(view->document);
        result.recovered = recovered(view->document);
    }
    result.externalConflict = result.explicitOverwriteRequired ||
                              result.externalChangeState == storage::FileChangeState::Modified ||
                              result.externalChangeState == storage::FileChangeState::Deleted ||
                              result.externalChangeState == storage::FileChangeState::Inaccessible;
    return result;
}

std::vector<CommandPresentation> PresentationModel::buildCommands() const {
    const auto context = commandContext();
    std::vector<CommandPresentation> result;
    result.reserve(CommandCatalog::commands().size());
    for (const auto& command : CommandCatalog::commands()) {
        result.push_back({command.id, command.name, CommandCatalog::state(command.id, context)});
    }
    return result;
}

}
