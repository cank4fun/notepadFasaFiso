#include "notepadFasaFiso/app/PresentationModel.hpp"
#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/gui/FileDrop.hpp"
#include "notepadFasaFiso/gui/GuiShell.hpp"
#include "notepadFasaFiso/gui/GuiPolishModel.hpp"
#include "notepadFasaFiso/gui/SelectionAppearanceModel.hpp"
#include "notepadFasaFiso/gui/ColorWheelModel.hpp"
#include "notepadFasaFiso/gui/StartupSplash.hpp"
#include "notepadFasaFiso/settings/AppSettings.hpp"
#include "notepadFasaFiso/workspace/WorkspaceModel.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool near(const double left, const double right) {
    return std::abs(left - right) < 0.001;
}

void testPolishPresentationHelpers() {
    using namespace nff::gui;

    expect(formatPointSizeForUi(10.0) == "10", "whole point sizes avoid serialization decimals");
    expect(formatPointSizeForUi(10.5) == "10.5", "fractional point sizes stay human-readable");
    expect(formatPointSizeForUi(10.25) == "10.25", "meaningful fractional precision is preserved");
    expect(formatPointSizeForUi(10.125) == "10.125",
           "font-size formatting never truncates meaningful precision");
    expect(statusValueForUi("EOL", "Unknown") == "EOL: Unknown",
           "ambiguous status values gain a semantic label");
    expect(statusValueForUi("Mode", "Editor") == "Mode: Editor",
           "mode status is explicitly grouped");
    expect(confirmationDefaultButton() == ConfirmationButtonRole::Cancel,
           "destructive confirmations keep cancel as the safe default action");

    const auto linuxChrome = linuxChromeMetrics();
    expect(linuxChrome.menuPaddingX >= 12,
           "Linux menu chrome keeps readable horizontal button padding");
    expect(linuxChrome.menuGap >= 4,
           "Linux menu chrome keeps visible separation between menu buttons");

    NativeChangeLatch<int> nativeValue;
    expect(nativeValue.update(7), "first native value update is emitted");
    expect(!nativeValue.update(7), "identical native values are suppressed");
    expect(nativeValue.update(8), "changed native values are emitted");
    nativeValue.invalidate();
    expect(nativeValue.update(8), "invalidating a native value forces the next update");
    const auto pixels = pixelBounds({-4.8, 7.9, 120.9, 35.2});
    expect(pixels.x == 0 && pixels.y == 7 && pixels.width == 120 && pixels.height == 35,
           "pixel bounds preserve wx clamping and truncation rules");

    TextColorStyleRefreshLatch colorStyles;
    expect(colorStyles.needsRefresh(),
           "text color styles start dirty until first render");
    colorStyles.markRendered();
    expect(!colorStyles.needsRefresh(),
           "rendering text color styles clears the dirty state");
    colorStyles.invalidateAppearance();
    expect(colorStyles.needsRefresh(),
           "appearance changes invalidate text color styles even when metadata revision is unchanged");

    PointerDragLatch drag;
    expect(!drag.active(), "pointer drag starts inactive");
    drag.begin();
    expect(drag.active(), "pointer drag becomes active on press");
    drag.cancel();
    expect(!drag.active(), "capture loss cancels pointer drag state");
    drag.begin();
    drag.end();
    expect(!drag.active(), "normal pointer release ends pointer drag state");

    PointerPressLatch closePress;
    closePress.press(true);
    expect(!closePress.release(false),
           "pressing close then releasing outside cancels the close action");
    closePress.press(true);
    expect(closePress.release(true),
           "close action commits only when press and release both hit close");
    closePress.press(false);
    expect(!closePress.release(true),
           "releasing over close without pressing it first never closes a tab");

    const auto border = paneDropBorder({10.0, 20.0, 300.0, 200.0}, 2.0, 3.0);
    expect(near(border.top.x, 13.0) && near(border.top.y, 23.0) &&
               near(border.top.width, 294.0) && near(border.top.height, 2.0),
           "pane drop top edge is inset and thin");
    expect(near(border.left.x, 13.0) && near(border.left.y, 25.0) &&
               near(border.left.width, 2.0) && near(border.left.height, 190.0),
           "pane drop side edge avoids covering the content surface");
}

void testSplitterDragPreviewKeepsFullGrabbedBoundary() {
    const nff::gui::Rect verticalBoundary{500.0, 40.0, 6.0, 700.0};
    const auto vertical = nff::gui::localSplitterDragPreview(
        verticalBoundary, nff::gui::Point{503.0, 420.0}, true, 2.0, 72.0);
    expect(near(vertical.width, 2.0) && near(vertical.height, verticalBoundary.height) &&
               near(vertical.y, verticalBoundary.y),
           "vertical splitter drag preview keeps the full draggable boundary");

    const nff::gui::Rect horizontalBoundary{50.0, 300.0, 900.0, 6.0};
    const auto horizontal = nff::gui::localSplitterDragPreview(
        horizontalBoundary, nff::gui::Point{700.0, 303.0}, false, 2.0, 72.0);
    expect(near(horizontal.height, 2.0) && near(horizontal.width, horizontalBoundary.width) &&
               near(horizontal.x, horizontalBoundary.x),
           "horizontal splitter drag preview keeps the full draggable boundary");
}

void testSinglePaneShellLayoutMatchesSketchStructure() {
    nff::core::DocumentManager documents;
    const auto document = documents.createUntitled();
    nff::workspace::WorkspaceModel workspace;
    const auto view = workspace.openView(document, workspace.primaryPane());
    expect(static_cast<bool>(view), "open initial view");

    nff::settings::AppSettings settings;
    settings.tabsEnabled = true;
    settings.sidebarVisible = true;
    nff::app::PresentationModel presentation(documents, workspace, settings);
    presentation.setSidebarWidth(280.0);

    nff::gui::GuiShell shell(workspace, presentation);
    shell.setViewport({1200.0, 800.0});
    static_cast<void>(shell.refresh());
    const auto& frame = shell.frame();

    expect(frame.layout.panes.size() == 1U, "single pane layout");
    expect(near(frame.layout.titleBar.height, 30.0), "custom title bar has independent strip");
    expect(near(frame.layout.topBar.y, 30.0), "menu bar starts below custom title bar");
    expect(near(frame.layout.topBar.height, 30.0), "top bar has independent strip");
    expect(near(frame.layout.sidebarSearch.width, 280.0), "sidebar search uses requested width");
    expect(near(frame.layout.sidebarSplitter.width, 4.0), "sidebar has resize grip");
    expect(near(frame.layout.statusBar.height, 24.0), "status bar is global");
    expect(near(frame.layout.statusBar.x, 284.0), "status bar starts after sidebar");
    expect(near(frame.layout.panes.front().tabStrip.height, 34.0), "pane owns tab strip");
    expect(frame.layout.panes.front().content.height > 0.0, "pane has editor content region");

    expect(shell.hitTest({600.0, 15.0}).region == nff::gui::HitRegion::TitleBar,
           "custom title bar has a distinct hit-test region");
    expect(shell.hitTest({600.0, 45.0}).region == nff::gui::HitRegion::TopBar,
           "menu bar remains independently hittable");

    const auto sidebarHit = shell.hitTest({279.0, 100.0});
    expect(sidebarHit.region == nff::gui::HitRegion::SidebarBody, "hit sidebar body");
    const auto gripHit = shell.hitTest({281.0, 100.0});
    expect(gripHit.region == nff::gui::HitRegion::SidebarSplitter, "hit sidebar resize grip");
}

void testRecursiveSplitsProduceIndependentPaneRects() {
    nff::core::DocumentManager documents;
    const auto firstDocument = documents.createUntitled();
    const auto secondDocument = documents.createUntitled();
    const auto thirdDocument = documents.createUntitled();

    nff::workspace::WorkspaceModel workspace;
    const auto firstPane = workspace.primaryPane();
    const auto firstView = workspace.openView(firstDocument, firstPane);
    expect(static_cast<bool>(firstView), "open first split view");

    const auto right = workspace.splitPane(
        firstPane, nff::workspace::SplitOrientation::Horizontal,
        nff::workspace::SplitPlacement::After, 0.6);
    expect(static_cast<bool>(right), "create side-by-side split");
    const auto secondView = workspace.openView(secondDocument, right.pane);
    expect(static_cast<bool>(secondView), "open second split view");

    const auto lower = workspace.splitPane(
        right.pane, nff::workspace::SplitOrientation::Vertical,
        nff::workspace::SplitPlacement::After, 0.5);
    expect(static_cast<bool>(lower), "create nested vertical split");
    const auto thirdView = workspace.openView(thirdDocument, lower.pane);
    expect(static_cast<bool>(thirdView), "open third split view");

    nff::settings::AppSettings settings;
    nff::app::PresentationModel presentation(documents, workspace, settings);
    nff::gui::GuiShell shell(workspace, presentation);
    shell.setViewport({1600.0, 900.0});
    shell.setSidebarVisible(false);
    static_cast<void>(shell.refresh());

    const auto& frame = shell.frame();
    expect(frame.layout.panes.size() == 3U, "recursive split creates three pane layouts");
    expect(frame.layout.splitters.size() == 2U, "recursive split creates two resize grips");

    bool horizontalSeen = false;
    bool verticalSeen = false;
    for (const auto& splitter : frame.layout.splitters) {
        horizontalSeen = horizontalSeen ||
                         splitter.orientation == nff::workspace::SplitOrientation::Horizontal;
        verticalSeen = verticalSeen ||
                       splitter.orientation == nff::workspace::SplitOrientation::Vertical;
        const nff::gui::Point center{
            splitter.bounds.x + splitter.bounds.width * 0.5,
            splitter.bounds.y + splitter.bounds.height * 0.5,
        };
        const auto hit = shell.hitTest(center);
        expect(hit.region == nff::gui::HitRegion::WorkspaceSplitter,
               "splitter hit testing returns workspace splitter");
        expect(hit.split == splitter.split, "splitter hit keeps split id");
        expect(splitter.track.width >= splitter.bounds.width &&
                   splitter.track.height >= splitter.bounds.height,
               "splitter exposes its full drag track");
    }
    expect(horizontalSeen && verticalSeen, "both split orientations preserved");
}

void testStandardTwoByTwoSplitCreatesIndependentRowBoundaries() {
    nff::core::DocumentManager documents;
    const auto document = documents.createUntitled();
    nff::workspace::WorkspaceModel workspace;
    const auto paneA = workspace.primaryPane();
    static_cast<void>(workspace.openView(document, paneA));

    nff::settings::AppSettings settings;
    settings.sidebarVisible = false;
    nff::app::PresentationModel presentation(documents, workspace, settings);
    nff::gui::GuiShell shell(workspace, presentation);
    shell.setViewport({1200.0, 800.0});

    const auto right = shell.splitPane(
        paneA, nff::workspace::SplitOrientation::Horizontal,
        nff::workspace::SplitPlacement::After, 0.5);
    const auto lowerLeft = shell.splitPane(
        paneA, nff::workspace::SplitOrientation::Vertical,
        nff::workspace::SplitPlacement::After, 0.5);
    const auto lowerRight = shell.splitPane(
        right.pane, nff::workspace::SplitOrientation::Vertical,
        nff::workspace::SplitPlacement::After, 0.5);
    expect(static_cast<bool>(right) && static_cast<bool>(lowerLeft) &&
               static_cast<bool>(lowerRight),
           "construct standard 2x2 through shell");

    static_cast<void>(shell.refresh());
    const auto& root = workspace.root();
    expect(root.orientation() == nff::workspace::SplitOrientation::Vertical &&
               root.first() != nullptr && root.second() != nullptr &&
               root.first()->orientation() == nff::workspace::SplitOrientation::Horizontal &&
               root.second()->orientation() == nff::workspace::SplitOrientation::Horizontal,
           "standard shell 2x2 normalizes into independent rows");

    const auto topSplit = root.first() == nullptr ? nff::workspace::SplitId{} : root.first()->split();
    const auto bottomSplit = root.second() == nullptr ? nff::workspace::SplitId{} : root.second()->split();
    expect(shell.setSplitRatio(topSplit, 0.35), "resize only top row through shell");
    static_cast<void>(shell.refresh());
    expect(workspace.root().first() != nullptr && workspace.root().second() != nullptr &&
               near(workspace.root().first()->ratio(), 0.35) &&
               near(workspace.root().second()->ratio(), 0.5),
           "shell top-row resize does not move bottom-row boundary");

    std::size_t horizontalSegments = 0U;
    for (const auto& splitter : shell.frame().layout.splitters) {
        if (splitter.orientation == nff::workspace::SplitOrientation::Horizontal) {
            ++horizontalSegments;
            expect(splitter.bounds.height < shell.frame().layout.workspace.height,
                   "row horizontal splitter is a local segment rather than full-height boundary");
        }
    }
    expect(horizontalSegments == 2U, "2x2 exposes two independent left-right splitter segments");
    static_cast<void>(bottomSplit);
}

void testAlignedTwoByTwoCanPivotToIndependentColumns() {
    nff::core::DocumentManager documents;
    const auto document = documents.createUntitled();
    nff::workspace::WorkspaceModel workspace;
    const auto paneA = workspace.primaryPane();
    static_cast<void>(workspace.openView(document, paneA));
    nff::settings::AppSettings settings;
    settings.sidebarVisible = false;
    nff::app::PresentationModel presentation(documents, workspace, settings);
    nff::gui::GuiShell shell(workspace, presentation);
    shell.setViewport({1200.0, 800.0});

    const auto right = shell.splitPane(
        paneA, nff::workspace::SplitOrientation::Horizontal,
        nff::workspace::SplitPlacement::After, 0.5);
    static_cast<void>(shell.splitPane(
        paneA, nff::workspace::SplitOrientation::Vertical,
        nff::workspace::SplitPlacement::After, 0.5));
    static_cast<void>(shell.splitPane(
        right.pane, nff::workspace::SplitOrientation::Vertical,
        nff::workspace::SplitPlacement::After, 0.5));
    static_cast<void>(shell.refresh());

    expect(shell.normalizeAlignedTwoByTwoColumns(),
           "aligned shell 2x2 pivots to independent column splitters");
    static_cast<void>(shell.refresh());
    const auto& root = workspace.root();
    expect(root.orientation() == nff::workspace::SplitOrientation::Horizontal &&
               root.first() != nullptr && root.second() != nullptr &&
               root.first()->orientation() == nff::workspace::SplitOrientation::Vertical &&
               root.second()->orientation() == nff::workspace::SplitOrientation::Vertical,
           "shell column pivot exposes two local top-bottom splitters");

    const auto leftSplit = root.first()->split();
    const auto rightSplit = root.second()->split();
    expect(shell.setSplitRatio(leftSplit, 0.35), "resize only left column after pivot");
    expect(near(workspace.root().first()->ratio(), 0.35) &&
               near(workspace.root().second()->ratio(), 0.5),
           "left top-bottom resize leaves right column unchanged");
    expect(!shell.normalizeAlignedTwoByTwoRows(),
           "perpendicular pivot is rejected while column row boundaries are misaligned");

    expect(shell.setSplitRatio(rightSplit, 0.35), "realign right column for reciprocal pivot");
    expect(shell.normalizeAlignedTwoByTwoRows(),
           "reciprocal row pivot is available again after magnet-style alignment");
}

void testShellMutationsDriveWorkspaceWithoutOwningDrawing() {
    nff::core::DocumentManager documents;
    const auto documentA = documents.createUntitled();
    const auto documentB = documents.createUntitled();
    nff::workspace::WorkspaceModel workspace;
    const auto paneA = workspace.primaryPane();
    const auto viewA = workspace.openView(documentA, paneA);
    const auto viewB = workspace.openView(documentB, paneA);

    nff::settings::AppSettings settings;
    nff::app::PresentationModel presentation(documents, workspace, settings);
    nff::gui::GuiShell shell(workspace, presentation);

    expect(shell.activateView(viewA), "activate tab through shell");
    const auto split = shell.splitPane(
        paneA, nff::workspace::SplitOrientation::Horizontal,
        nff::workspace::SplitPlacement::After, 0.5);
    expect(static_cast<bool>(split), "split through shell");
    expect(shell.moveView(viewB, split.pane), "move tab between panes through shell");
    expect(shell.activateView(viewB), "activate moved tab");
    expect(workspace.activePane() == split.pane, "active pane follows moved view");

    shell.setSidebarWidth(430.0);
    shell.setViewport({1400.0, 800.0});
    const auto& frame = shell.refresh();
    expect(near(frame.layout.sidebarSearch.width, 430.0), "custom sidebar width reaches layout");
    expect(frame.generation > 0U, "refresh advances shell generation");

    expect(shell.closeView(viewB), "close view through shell");
    expect(workspace.paneCount() == 1U, "empty secondary pane collapses automatically");
}

void testSplitDuplicatePreservesViewAndRuntimeState() {
    nff::core::DocumentManager documents;
    const auto document = documents.createUntitled();

    nff::workspace::WorkspaceModel workspace;
    const auto sourcePane = workspace.primaryPane();
    const auto sourceView = workspace.openView(document, sourcePane);
    expect(static_cast<bool>(sourceView), "open source view for split duplication");

    auto* sourceState = workspace.view(sourceView);
    expect(sourceState != nullptr, "source split view state exists");
    if (sourceState == nullptr) {
        return;
    }
    sourceState->caretOffset = 41U;
    sourceState->anchorOffset = 17U;
    sourceState->firstVisibleLine = 9U;
    sourceState->wordWrapOverride = false;
    sourceState->lineNumbersOverride = true;
    sourceState->fontFamilyOverride = "Split Test Mono";
    sourceState->fontPointSizeOverride = 17.0;

    nff::settings::AppSettings settings;
    nff::app::PresentationModel presentation(documents, workspace, settings);

    const auto* initialRuntime = presentation.viewRuntime(sourceView);
    expect(initialRuntime != nullptr, "source runtime exists before split duplication");
    if (initialRuntime == nullptr) {
        return;
    }
    auto runtime = *initialRuntime;
    runtime.openMode = nff::core::OpenMode::Viewer;
    runtime.viewerPerformance = nff::viewer::PerformanceProfile::MemorySaver;
    runtime.resolvedViewerPerformance = nff::viewer::PerformanceProfile::MemorySaver;
    runtime.followEnabled = true;
    runtime.wordWrap = false;
    runtime.lineNumbersVisible = true;
    runtime.fontFamily = "Split Test Mono";
    runtime.fontPointSize = 17.0;
    expect(presentation.setViewRuntime(sourceView, runtime),
           "configure source runtime before split duplication");

    nff::gui::GuiShell shell(workspace, presentation);
    const auto split = shell.splitPane(
        sourcePane, nff::workspace::SplitOrientation::Horizontal,
        nff::workspace::SplitPlacement::After, 0.5);
    expect(static_cast<bool>(split), "create target pane for split duplication");

    const auto duplicate = shell.duplicateView(sourceView, split.pane);
    expect(static_cast<bool>(duplicate), "duplicate active view into split pane");
    expect(duplicate != sourceView, "split duplicate gets a distinct view id");

    const auto* duplicateState = workspace.view(duplicate);
    expect(duplicateState != nullptr && duplicateState->document == document,
           "split duplicate keeps the same document");
    expect(duplicateState != nullptr && duplicateState->caretOffset == 41U &&
               duplicateState->anchorOffset == 17U &&
               duplicateState->firstVisibleLine == 9U,
           "split duplicate preserves independent viewport state");
    expect(duplicateState != nullptr &&
               duplicateState->wordWrapOverride == sourceState->wordWrapOverride &&
               duplicateState->lineNumbersOverride == sourceState->lineNumbersOverride &&
               duplicateState->fontFamilyOverride == sourceState->fontFamilyOverride &&
               duplicateState->fontPointSizeOverride == sourceState->fontPointSizeOverride,
           "split duplicate preserves per-view appearance overrides");

    const auto* duplicateRuntime = presentation.viewRuntime(duplicate);
    expect(duplicateRuntime != nullptr &&
               duplicateRuntime->openMode == nff::core::OpenMode::Viewer &&
               duplicateRuntime->viewerPerformance ==
                   nff::viewer::PerformanceProfile::MemorySaver &&
               duplicateRuntime->followEnabled &&
               !duplicateRuntime->wordWrap &&
               duplicateRuntime->lineNumbersVisible &&
               duplicateRuntime->fontFamily == "Split Test Mono" &&
               near(duplicateRuntime->fontPointSize, 17.0),
           "split duplicate preserves runtime mode and view behavior");

    const auto* targetPane = workspace.pane(split.pane);
    expect(targetPane != nullptr && targetPane->activeView == duplicate &&
               workspace.activePane() == split.pane,
           "split duplicate becomes active in the target pane");
}

void testOpenDocumentInPaneReusesTargetViewWithoutStealingOtherPaneView() {
    nff::core::DocumentManager documents;
    const auto document = documents.createUntitled();
    nff::workspace::WorkspaceModel workspace;
    const auto sourcePane = workspace.primaryPane();
    const auto sourceView = workspace.openView(document, sourcePane);
    const auto split = workspace.splitPane(
        sourcePane, nff::workspace::SplitOrientation::Horizontal,
        nff::workspace::SplitPlacement::After, 0.5);

    nff::settings::AppSettings settings;
    nff::app::PresentationModel presentation(documents, workspace, settings);
    nff::gui::GuiShell shell(workspace, presentation);

    const auto targetView = shell.openDocumentInPane(document, split.pane);
    expect(static_cast<bool>(targetView) && targetView != sourceView,
           "target-pane open creates an independent view instead of stealing source view");
    expect(workspace.paneContaining(sourceView) == sourcePane &&
               workspace.paneContaining(targetView) == split.pane,
           "source and target views keep independent pane ownership");
    expect(workspace.view(targetView) != nullptr &&
               workspace.view(targetView)->document == document,
           "target-pane view shares the existing document identity");

    const auto countBeforeReuse = workspace.viewCount();
    const auto reusedTarget = shell.openDocumentInPane(document, split.pane);
    expect(reusedTarget == targetView && workspace.viewCount() == countBeforeReuse,
           "reopening same document in target pane activates existing target view");
    expect(workspace.activePane() == split.pane,
           "target-pane document open activates requested pane");
}

void testClosePaneClosesAllViewsAndCollapsesTree() {
    nff::core::DocumentManager documents;
    const auto documentA = documents.createUntitled();
    const auto documentB = documents.createUntitled();
    const auto documentC = documents.createUntitled();

    nff::workspace::WorkspaceModel workspace;
    const auto primary = workspace.primaryPane();
    const auto primaryView = workspace.openView(documentA, primary);
    const auto split = workspace.splitPane(
        primary, nff::workspace::SplitOrientation::Horizontal,
        nff::workspace::SplitPlacement::After, 0.5);
    const auto secondView = workspace.openView(documentB, split.pane);
    const auto thirdView = workspace.openView(documentC, split.pane);
    expect(static_cast<bool>(primaryView) && static_cast<bool>(secondView) &&
               static_cast<bool>(thirdView),
           "open views for close-pane test");

    nff::settings::AppSettings settings;
    nff::app::PresentationModel presentation(documents, workspace, settings);
    nff::gui::GuiShell shell(workspace, presentation);

    const auto closed = shell.closePane(split.pane);
    expect(closed.size() == 2U, "close pane reports every closed view");
    expect(workspace.pane(split.pane) == nullptr, "closed pane removed from workspace");
    expect(workspace.paneCount() == 1U, "split tree collapses after close pane");
    expect(workspace.view(secondView) == nullptr && workspace.view(thirdView) == nullptr,
           "pane views removed from workspace");
    expect(workspace.view(primaryView) != nullptr, "other pane view survives close pane");
    expect(shell.closePane(primary).empty(), "cannot close the final remaining pane");
}

void testExternalNoticeReservesWorkspaceWithoutMovingSidebar() {
    nff::core::DocumentManager documents;
    const auto document = documents.createUntitled();
    nff::workspace::WorkspaceModel workspace;
    static_cast<void>(workspace.openView(document, workspace.primaryPane()));

    nff::settings::AppSettings settings;
    settings.sidebarVisible = true;
    nff::app::PresentationModel presentation(documents, workspace, settings);
    presentation.setExternalChangeState(document, nff::storage::FileChangeState::Modified);

    nff::gui::GuiShell shell(workspace, presentation);
    shell.setViewport({1200.0, 800.0});
    static_cast<void>(shell.refresh());
    const auto& layout = shell.frame().layout;

    expect(near(layout.externalNotice.height, 34.0),
           "external conflict gets a compact non-modal notice strip");
    expect(near(layout.externalNotice.x, layout.workspace.x),
           "external notice belongs to workspace, not sidebar");
    expect(near(layout.workspace.y, layout.externalNotice.bottom()),
           "workspace starts below external notice");
    expect(near(layout.sidebarSearch.y, 60.0),
           "external notice does not shift title/menu chrome or sidebar search");

    const nff::gui::Point noticeCenter{
        layout.externalNotice.x + layout.externalNotice.width * 0.5,
        layout.externalNotice.y + layout.externalNotice.height * 0.5,
    };
    expect(shell.hitTest(noticeCenter).region == nff::gui::HitRegion::ExternalNotice,
           "external notice has a distinct hit-test region");

    presentation.setExternalChangeState(document, nff::storage::FileChangeState::Unchanged);
    shell.requestRefresh();
    static_cast<void>(shell.refresh());
    expect(shell.frame().layout.externalNotice.empty(),
           "notice disappears after conflict is resolved");

    presentation.setRecovered(document, true);
    shell.requestRefresh();
    static_cast<void>(shell.refresh());
    expect(near(shell.frame().layout.externalNotice.height, 34.0),
           "recovered document also gets the compact notice strip");
    presentation.setRecovered(document, false);
    shell.requestRefresh();
    static_cast<void>(shell.refresh());
    expect(shell.frame().layout.externalNotice.empty(),
           "recovery notice disappears after recovery is resolved");
}

void testOuterFrameInsetReservesSafeResizeZone() {
    nff::core::DocumentManager documents;
    const auto document = documents.createUntitled();
    nff::workspace::WorkspaceModel workspace;
    static_cast<void>(workspace.openView(document, workspace.primaryPane()));

    nff::settings::AppSettings settings;
    settings.sidebarVisible = false;
    nff::app::PresentationModel presentation(documents, workspace, settings);
    nff::gui::GuiShell shell(workspace, presentation);

    auto metrics = shell.layoutMetrics();
    metrics.outerFrameInset = 3.0;
    expect(shell.setLayoutMetrics(metrics), "accept non-negative outer frame inset");
    shell.setViewport({1200.0, 800.0});
    static_cast<void>(shell.refresh());

    const auto& layout = shell.frame().layout;
    expect(near(layout.contentFrame.x, 3.0) && near(layout.contentFrame.y, 3.0),
           "content frame starts inside the resize-safe margin");
    expect(near(layout.contentFrame.width, 1194.0) && near(layout.contentFrame.height, 794.0),
           "content frame reserves the resize-safe margin on all four edges");
    expect(near(layout.titleBar.x, 3.0) && near(layout.titleBar.y, 3.0),
           "custom title bar stays inside the safe frame");
    expect(near(layout.statusBar.bottom(), 797.0),
           "status bar stays clear of the bottom resize margin");
    expect(shell.hitTest({1.0, 400.0}).region == nff::gui::HitRegion::FrameBorder,
           "outer margin has an explicit frame-border hit region");
    expect(shell.hitTest({600.0, 1.0}).region == nff::gui::HitRegion::FrameBorder,
           "top margin is reserved for frame interaction");
    expect(shell.hitTest({600.0, 20.0}).region == nff::gui::HitRegion::TitleBar,
           "title bar remains hittable inside the frame margin");

    metrics.outerFrameInset = -1.0;
    expect(!shell.setLayoutMetrics(metrics), "reject negative outer frame inset");
}

void testSplitterMagnetSnapsOnlyToNearestValidSameAxisBoundary() {
    nff::gui::ShellLayoutMetrics metrics;
    metrics.splitterThickness = 6.0;
    metrics.minimumPaneWidth = 120.0;
    metrics.minimumPaneHeight = 80.0;

    const nff::gui::SplitterLayout dragged{
        nff::workspace::SplitId{1},
        nff::workspace::SplitOrientation::Horizontal,
        {297.0, 0.0, 6.0, 300.0},
        {0.0, 0.0, 600.0, 300.0},
    };
    const std::vector<nff::gui::SplitterLayout> nearby{
        {nff::workspace::SplitId{2},
         nff::workspace::SplitOrientation::Horizontal,
         {312.0, 300.0, 6.0, 300.0},
         {0.0, 300.0, 600.0, 300.0}},
        {nff::workspace::SplitId{3},
         nff::workspace::SplitOrientation::Horizontal,
         {316.0, 600.0, 6.0, 300.0},
         {0.0, 600.0, 600.0, 300.0}},
        {nff::workspace::SplitId{4},
         nff::workspace::SplitOrientation::Vertical,
         {0.0, 312.0, 600.0, 6.0},
         {0.0, 0.0, 600.0, 600.0}},
    };

    const auto snapped = nff::gui::ShellLayoutEngine::snappedSplitterRatio(
        dragged, 0.52, nearby, metrics, 10.0);
    expect(snapped.has_value(), "splitter magnet finds same-axis boundary inside threshold");
    expect(snapped && near(*snapped, 312.0 / 594.0),
           "splitter magnet chooses nearest valid boundary coordinate");

    const auto far = nff::gui::ShellLayoutEngine::snappedSplitterRatio(
        dragged, 0.45, nearby, metrics, 10.0);
    expect(!far.has_value(), "splitter magnet releases outside threshold");

    const std::vector<nff::gui::SplitterLayout> invalidMinimum{
        {nff::workspace::SplitId{5},
         nff::workspace::SplitOrientation::Horizontal,
         {60.0, 300.0, 6.0, 300.0},
         {0.0, 300.0, 600.0, 300.0}},
    };
    const auto tooCloseToEdge = nff::gui::ShellLayoutEngine::snappedSplitterRatio(
        dragged, 60.0 / 594.0, invalidMinimum, metrics, 10.0);
    expect(!tooCloseToEdge.has_value(),
           "splitter magnet ignores alignment that violates minimum pane width");
}

void testStartupSplashLayoutScalesWithoutStretching() {
    const auto large = nff::gui::startupSplashLayout({3840.0, 2160.0});
    expect(near(large.window.width / large.window.height, 9.0 / 5.0),
           "startup splash preserves approved 9:5 composition");
    expect(large.window.width <= 960.0 && large.window.height <= 540.0,
           "startup splash stays capped on large displays");
    expect(large.wordmark.width < large.window.width &&
               large.tagline.width < large.wordmark.width,
           "startup splash content remains inside the vector canvas");
    expect(large.panel.x <= large.strokeWidth * 2.0 &&
               large.panel.y <= large.strokeWidth * 2.0,
           "startup splash border hugs the window edge");

    const auto compact = nff::gui::startupSplashLayout({800.0, 600.0});
    expect(compact.window.width <= 800.0 * 0.90 + 0.001,
           "startup splash shrinks for compact displays");
    expect(near(compact.window.width / compact.window.height, 9.0 / 5.0),
           "startup splash keeps aspect ratio when shrinking");

    expect(nff::gui::startupSplashCanRender("notepadFasaFiso"),
           "custom vector alphabet covers the product wordmark");
    expect(nff::gui::startupSplashCanRender("type some shi'"),
           "custom vector alphabet covers the splash tagline");
    expect(!nff::gui::startupSplashCanRender("type some shi'."),
           "approved splash tagline deliberately has no trailing period");
    expect(!nff::gui::startupSplashCanRender("@"),
           "custom vector alphabet rejects unsupported glyphs instead of font fallback");
}

void testInvalidMetricsAndTinyViewportStaySafe() {
    nff::core::DocumentManager documents;
    const auto document = documents.createUntitled();
    nff::workspace::WorkspaceModel workspace;
    static_cast<void>(workspace.openView(document, workspace.primaryPane()));
    nff::settings::AppSettings settings;
    nff::app::PresentationModel presentation(documents, workspace, settings);
    nff::gui::GuiShell shell(workspace, presentation);

    auto invalid = shell.layoutMetrics();
    invalid.splitterThickness = -1.0;
    expect(!shell.setLayoutMetrics(invalid), "reject negative layout metrics");

    invalid = shell.layoutMetrics();
    invalid.titleBarHeight = -1.0;
    expect(!shell.setLayoutMetrics(invalid), "reject negative title bar height");

    shell.setViewport({40.0, 20.0});
    static_cast<void>(shell.refresh());
    const auto& layout = shell.frame().layout;
    expect(layout.window.width == 40.0 && layout.window.height == 20.0,
           "tiny viewport is retained");
    expect(layout.workspace.width >= 0.0 && layout.workspace.height >= 0.0,
           "tiny viewport never produces negative workspace extents");
}

void testSelectionAppearanceRendererBudgetAndSpoilerRegion() {
    nff::metadata::TextAppearanceMap appearance;
    for (std::uint32_t index = 0; index < 128U; ++index) {
        appearance.setForeground(index * 2U, index * 2U + 1U, 0xFF000000U | index);
    }
    expect(nff::gui::renderedAppearanceCombinationCount(appearance) == 128U,
           "128 distinct font/color/size combinations fit the renderer budget");
    expect(nff::gui::appearanceFitsStyleBudget(appearance, 128U),
           "renderer accepts the exact 128-style budget");
    appearance.setForeground(300U, 301U, 0xFFABCDEFU);
    expect(!nff::gui::appearanceFitsStyleBudget(appearance, 128U),
           "renderer rejects the 129th distinct appearance combination");

    nff::metadata::TextAppearanceMap spoilers;
    spoilers.setSpoiler(10U, 15U, true);
    spoilers.setForeground(10U, 15U, 0xFFFF0000U);
    spoilers.setSpoiler(15U, 20U, true);
    spoilers.setForeground(15U, 20U, 0xFF00FF00U);
    const auto region = nff::gui::contiguousSpoilerRegionAt(spoilers.spans(), 17U);
    expect(region.has_value() && region->begin == 10U && region->end == 20U,
           "adjacent differently styled spoiler spans reveal as one contiguous region");
    expect(!nff::gui::contiguousSpoilerRegionAt(spoilers.spans(), 9U).has_value(),
           "pointer outside spoiler has no reveal region");
}

void testTabTopologyFingerprintChangesOnlyForNativeWidgetStructure() {
    const std::array<nff::gui::TabTopologyEntry, 4> topology{{
        {1U, 0U}, {1U, 10U}, {1U, 11U}, {2U, 20U},
    }};
    const auto baseline = nff::gui::tabTopologyFingerprint(true, topology);
    expect(baseline == nff::gui::tabTopologyFingerprint(true, topology),
           "identical tab topology keeps a stable native-widget fingerprint");

    auto reordered = topology;
    std::swap(reordered[1], reordered[2]);
    expect(baseline != nff::gui::tabTopologyFingerprint(true, reordered),
           "tab reorder changes native-widget topology fingerprint");

    auto movedPane = topology;
    movedPane[2].pane = 2U;
    expect(baseline != nff::gui::tabTopologyFingerprint(true, movedPane),
           "moving a tab between panes changes native-widget topology fingerprint");
    expect(baseline != nff::gui::tabTopologyFingerprint(false, topology),
           "hiding tabs changes native-widget topology fingerprint");
}

void testSelectionAppearanceSummaryDistinguishesUniformInheritedAndMixedProperties() {
    nff::metadata::TextAppearanceMap appearance;
    appearance.setForeground(0U, 10U, 0xFFFF0000U);
    const auto font = appearance.internFontFamily("Consolas");
    expect(font.has_value(), "selection appearance summary fixture interns font family");
    expect(appearance.setFontFamily(0U, 10U, *font),
           "selection appearance summary fixture applies font family");
    expect(appearance.setFontSize(0U, 10U, 14U),
           "selection appearance summary fixture applies font size");
    appearance.setSpoiler(0U, 10U, true);

    const auto uniform = nff::gui::summarizeSelectionAppearance(appearance, 2U, 8U);
    expect(uniform.has_value(), "non-empty selection has an appearance summary");
    expect(!uniform->foregroundArgb.mixed &&
               uniform->foregroundArgb.value == std::optional<std::uint32_t>{0xFFFF0000U},
           "uniform selection reports one foreground override");
    expect(!uniform->fontFamilyId.mixed && uniform->fontFamilyId.value == font,
           "uniform selection reports one font family override");
    expect(!uniform->fontSizePoints.mixed &&
               uniform->fontSizePoints.value == std::optional<std::uint8_t>{static_cast<std::uint8_t>(14U)},
           "uniform selection reports one font size override");
    expect(!uniform->spoiler.mixed && uniform->spoiler.value == std::optional<bool>{true},
           "uniform spoiler selection reports enabled state");

    const auto mixed = nff::gui::summarizeSelectionAppearance(appearance, 5U, 15U);
    expect(mixed.has_value(), "mixed selection has an appearance summary");
    expect(mixed->foregroundArgb.mixed && mixed->fontFamilyId.mixed &&
               mixed->fontSizePoints.mixed && mixed->spoiler.mixed,
           "formatted-to-inherited boundary reports every changed property as mixed");

    const auto inherited = nff::gui::summarizeSelectionAppearance(appearance, 12U, 18U);
    expect(inherited.has_value(), "inherited selection has an appearance summary");
    expect(!inherited->foregroundArgb.mixed && !inherited->foregroundArgb.value &&
               !inherited->fontFamilyId.mixed && !inherited->fontFamilyId.value &&
               !inherited->fontSizePoints.mixed && !inherited->fontSizePoints.value &&
               !inherited->spoiler.mixed && inherited->spoiler.value == std::optional<bool>{false},
           "unformatted selection reports inherited properties and spoiler off");

    expect(!nff::gui::summarizeSelectionAppearance(appearance, 4U, 4U).has_value(),
           "empty selection has no appearance summary");
}

void testColorWheelMathRoundTripsAndClampsPointer() {
    const nff::gui::RgbColor source{34U, 170U, 221U};
    const auto hsv = nff::gui::rgbToHsv(source);
    const auto roundTrip = nff::gui::hsvToRgb(hsv);
    expect(std::abs(static_cast<int>(roundTrip.red) - static_cast<int>(source.red)) <= 1 &&
               std::abs(static_cast<int>(roundTrip.green) - static_cast<int>(source.green)) <= 1 &&
               std::abs(static_cast<int>(roundTrip.blue) - static_cast<int>(source.blue)) <= 1,
           "RGB to HSV to RGB stays within one channel step");

    const auto center = nff::gui::colorWheelPointToHsv(50.0, 50.0, 50.0, 50.0, 40.0, 0.75);
    expect(near(center.saturation, 0.0) && near(center.value, 0.75),
           "wheel center maps to zero saturation while preserving value");
    const auto outside = nff::gui::colorWheelPointToHsv(200.0, 50.0, 50.0, 50.0, 40.0, 1.0);
    expect(near(outside.saturation, 1.0), "wheel pointer outside radius clamps to full saturation");
}

void testFileDropPreparationKeepsOnlyRegularFilesInOrder() {
    const auto root = std::filesystem::temp_directory_path() / "nff-file-drop-test";
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    std::filesystem::create_directories(root / "folder");

    const auto first = root / "first.txt";
    const auto second = root / "second.bin";
    {
        std::ofstream stream(first, std::ios::binary | std::ios::trunc);
        stream << "first";
    }
    {
        std::ofstream stream(second, std::ios::binary | std::ios::trunc);
        stream << "second";
    }

    const std::vector<std::filesystem::path> candidates{
        first,
        root / "folder",
        root / "missing.txt",
        second,
        {},
    };
    const auto prepared = nff::gui::prepareFileDropPaths(candidates);
    expect(prepared.files.size() == 2U, "file drop keeps only regular files");
    expect(prepared.files.size() >= 2U && prepared.files[0] == first.lexically_normal(),
           "file drop preserves first file order");
    expect(prepared.files.size() >= 2U && prepared.files[1] == second.lexically_normal(),
           "file drop preserves second file order");
    expect(prepared.ignoredEntries == 3U,
           "file drop counts directory missing and empty entries as ignored");

    std::filesystem::remove_all(root, cleanupError);
}

}

int main() {
    testPolishPresentationHelpers();
    testSplitterDragPreviewKeepsFullGrabbedBoundary();
    testSinglePaneShellLayoutMatchesSketchStructure();
    testRecursiveSplitsProduceIndependentPaneRects();
    testStandardTwoByTwoSplitCreatesIndependentRowBoundaries();
    testAlignedTwoByTwoCanPivotToIndependentColumns();
    testShellMutationsDriveWorkspaceWithoutOwningDrawing();
    testSplitDuplicatePreservesViewAndRuntimeState();
    testOpenDocumentInPaneReusesTargetViewWithoutStealingOtherPaneView();
    testClosePaneClosesAllViewsAndCollapsesTree();
    testExternalNoticeReservesWorkspaceWithoutMovingSidebar();
    testOuterFrameInsetReservesSafeResizeZone();
    testSplitterMagnetSnapsOnlyToNearestValidSameAxisBoundary();
    testStartupSplashLayoutScalesWithoutStretching();
    testInvalidMetricsAndTinyViewportStaySafe();
    testSelectionAppearanceRendererBudgetAndSpoilerRegion();
    testTabTopologyFingerprintChangesOnlyForNativeWidgetStructure();
    testSelectionAppearanceSummaryDistinguishesUniformInheritedAndMixedProperties();
    testColorWheelMathRoundTripsAndClampsPointer();
    testFileDropPreparationKeepsOnlyRegularFilesInOrder();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "all gui shell tests passed\n";
    return 0;
}
