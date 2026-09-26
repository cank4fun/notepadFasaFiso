#include "BenchmarkSupport.hpp"

#include "notepadFasaFiso/app/DocumentFileMonitor.hpp"
#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/core/SaveOptions.hpp"
#include "notepadFasaFiso/recovery/RecoveryManager.hpp"
#include "notepadFasaFiso/search/FileSearch.hpp"
#include "notepadFasaFiso/session/SessionRestorer.hpp"
#include "notepadFasaFiso/session/SessionStore.hpp"
#include "notepadFasaFiso/storage/FileWatcher.hpp"
#include "notepadFasaFiso/storage/FileWriter.hpp"
#include "notepadFasaFiso/viewer/LargeFileViewer.hpp"
#include "notepadFasaFiso/viewer/LiveFileFollower.hpp"
#include "notepadFasaFiso/workspace/WorkspaceModel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using nff::bench::BenchmarkResult;
using nff::bench::TempFixture;
constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;

[[nodiscard]] std::span<const std::byte> asBytes(const std::string_view text) {
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

[[nodiscard]] BenchmarkResult documentOpenClose(const std::size_t iterations) {
    TempFixture fixture("nff-stress-open-close");
    const auto path = fixture.path("document.txt");
    const auto error = nff::bench::writeRepeatedText(path, "open close stress line\n", 256U * 1024U);
    if (error) {
        return {"document_open_close", {}, false, error.message()};
    }

    nff::core::DocumentManager documents;
    for (std::size_t index = 0U; index < iterations; ++index) {
        const auto opened = documents.open(path);
        if (!opened || documents.size() != 1U || !documents.close(opened.id) || !documents.empty()) {
            return {"document_open_close", {}, false, "document manager leaked or failed an open/close cycle"};
        }
    }
    BenchmarkResult result;
    result.name = "document_open_close";
    result.measurements.push_back(nff::bench::measure("iterations", static_cast<std::uint64_t>(iterations)));
    return result;
}

[[nodiscard]] BenchmarkResult saveReloadConflict(const std::size_t iterations) {
    TempFixture fixture("nff-stress-save-reload");
    const auto path = fixture.path("conflict.txt");
    auto error = nff::storage::FileWriter::writeAtomically(path, asBytes("initial\n"));
    if (error) {
        return {"save_reload_conflict", {}, false, error.message()};
    }

    nff::core::DocumentManager documents;
    const auto opened = documents.open(path);
    if (!opened) {
        return {"save_reload_conflict", {}, false, opened.error.message()};
    }
    auto* document = documents.get(opened.id);
    if (document == nullptr) {
        return {"save_reload_conflict", {}, false, "document missing after open"};
    }

    std::size_t conflicts = 0U;
    for (std::size_t index = 0U; index < iterations; ++index) {
        document->replaceText("local-" + std::to_string(index) + "\n");
        if ((index % 7U) == 0U) {
            const auto external = "remote-" + std::to_string(index) + "\n";
            error = nff::storage::FileWriter::writeAtomically(path, asBytes(external));
            if (error) {
                return {"save_reload_conflict", {}, false, error.message()};
            }
            const auto refused = document->save();
            if (refused != std::make_error_code(std::errc::text_file_busy)) {
                return {"save_reload_conflict", {}, false, "external replacement was not refused"};
            }
            ++conflicts;
            nff::core::SaveOptions overwrite;
            overwrite.allowExternalOverwrite = true;
            error = document->save(overwrite);
        } else {
            error = document->save();
        }
        if (error || document->modified()) {
            return {"save_reload_conflict", {}, false,
                    error ? error.message() : "document remained modified after successful save"};
        }
        error = documents.reloadRouted(opened.id);
        if (error || document->text() != "local-" + std::to_string(index) + "\n") {
            return {"save_reload_conflict", {}, false,
                    error ? error.message() : "reload changed saved content"};
        }
    }

    BenchmarkResult result;
    result.name = "save_reload_conflict";
    result.measurements.push_back(nff::bench::measure("iterations", static_cast<std::uint64_t>(iterations)));
    result.measurements.push_back(nff::bench::measure("conflicts", static_cast<std::uint64_t>(conflicts)));
    return result;
}

[[nodiscard]] BenchmarkResult recoveryCycle(const std::size_t iterations) {
    TempFixture fixture("nff-stress-recovery");
    nff::core::DocumentManager documents;
    const auto id = documents.createUntitled();
    auto* document = documents.get(id);
    if (document == nullptr) {
        return {"recovery_cycle", {}, false, "unable to create recovery document"};
    }
    nff::recovery::RecoveryManager recovery(fixture.root() / "recovery", 6900U);

    for (std::size_t index = 0U; index < iterations; ++index) {
        const auto expected = "recovery-payload-" + std::to_string(index) + "\n";
        document->replaceText(expected);
        auto error = recovery.checkpoint(id, *document);
        if (error) {
            return {"recovery_cycle", {}, false, error.message()};
        }
        const auto loaded = recovery.load(recovery.snapshotPath(id));
        if (!loaded || loaded.snapshot.text != expected || loaded.snapshot.documentId != id) {
            return {"recovery_cycle", {}, false,
                    loaded.error ? loaded.error.message() : "recovery snapshot fidelity failure"};
        }
        error = recovery.discard(id);
        if (error || std::filesystem::exists(recovery.snapshotPath(id))) {
            return {"recovery_cycle", {}, false,
                    error ? error.message() : "recovery snapshot remained after discard"};
        }
    }
    BenchmarkResult result;
    result.name = "recovery_cycle";
    result.measurements.push_back(nff::bench::measure("iterations", static_cast<std::uint64_t>(iterations)));
    return result;
}

[[nodiscard]] BenchmarkResult sessionRestoreCycle(const std::size_t iterations) {
    TempFixture fixture("nff-stress-session");
    const auto leftPath = fixture.path("left.txt");
    const auto rightPath = fixture.path("right.txt");
    auto error = nff::storage::FileWriter::writeAtomically(leftPath, asBytes("left\n"));
    if (!error) {
        error = nff::storage::FileWriter::writeAtomically(rightPath, asBytes("right\n"));
    }
    if (error) {
        return {"session_restore_cycle", {}, false, error.message()};
    }

    nff::core::DocumentManager sourceDocuments;
    const auto left = sourceDocuments.open(leftPath);
    const auto right = sourceDocuments.open(rightPath);
    if (!left || !right) {
        return {"session_restore_cycle", {}, false, "unable to open session fixtures"};
    }
    nff::workspace::WorkspaceModel sourceWorkspace;
    const auto leftView = sourceWorkspace.openView(left.id, sourceWorkspace.primaryPane());
    const auto split = sourceWorkspace.splitPane(sourceWorkspace.primaryPane(),
                                                 nff::workspace::SplitOrientation::Horizontal);
    const auto rightView = split ? sourceWorkspace.openView(right.id, split.pane) : nff::workspace::ViewId{};
    if (!leftView || !split || !rightView) {
        return {"session_restore_cycle", {}, false, "unable to construct session workspace"};
    }

    const auto sessionPath = fixture.path("session.nff");
    for (std::size_t index = 0U; index < iterations; ++index) {
        const auto captured = nff::session::SessionStore::capture(sourceDocuments, sourceWorkspace);
        error = nff::session::SessionStore::save(sessionPath, captured);
        if (error) {
            return {"session_restore_cycle", {}, false, error.message()};
        }
        const auto loaded = nff::session::SessionStore::load(sessionPath);
        if (!loaded) {
            return {"session_restore_cycle", {}, false, loaded.error.message()};
        }
        nff::core::DocumentManager targetDocuments;
        nff::workspace::WorkspaceModel targetWorkspace;
        const auto restored = nff::session::SessionRestorer::restore(
            loaded.state, targetDocuments, targetWorkspace);
        if (!restored.workspaceRestored || restored.usedFallbackWorkspace ||
            targetDocuments.size() != 2U || targetWorkspace.paneCount() != 2U ||
            targetWorkspace.viewCount() != 2U) {
            return {"session_restore_cycle", {}, false, "session topology/count drift detected"};
        }
    }
    BenchmarkResult result;
    result.name = "session_restore_cycle";
    result.measurements.push_back(nff::bench::measure("iterations", static_cast<std::uint64_t>(iterations)));
    return result;
}

[[nodiscard]] BenchmarkResult workspaceChurn(const std::size_t iterations) {
    nff::core::DocumentManager documents;
    const std::array<nff::core::DocumentId, 4> documentIds{
        documents.createUntitled(), documents.createUntitled(), documents.createUntitled(), documents.createUntitled()};
    nff::workspace::WorkspaceModel workspace;
    const auto primary = workspace.primaryPane();
    const auto right = workspace.splitPane(primary, nff::workspace::SplitOrientation::Horizontal);
    const auto lowerLeft = workspace.splitPane(primary, nff::workspace::SplitOrientation::Vertical);
    const auto lowerRight = right ? workspace.splitPane(right.pane, nff::workspace::SplitOrientation::Vertical)
                                  : nff::workspace::SplitResult{};
    if (!right || !lowerLeft || !lowerRight || workspace.paneCount() != 4U) {
        return {"workspace_churn", {}, false, "unable to create four-pane workspace"};
    }
    const std::array<nff::workspace::PaneId, 4> panes{primary, right.pane, lowerLeft.pane, lowerRight.pane};
    for (std::size_t index = 0U; index < documentIds.size(); ++index) {
        if (!workspace.openView(documentIds[index], panes[index])) {
            return {"workspace_churn", {}, false, "unable to seed workspace views"};
        }
    }

    for (std::size_t index = 0U; index < iterations; ++index) {
        const auto pane = panes[index % panes.size()];
        const auto target = panes[(index + 1U) % panes.size()];
        const auto temporary = workspace.openView(documentIds[index % documentIds.size()], pane);
        if (!temporary || !workspace.moveView(temporary, target) || !workspace.closeView(temporary)) {
            return {"workspace_churn", {}, false, "view open/move/close churn failed"};
        }
        const auto snapshot = workspace.snapshot();
        if (!workspace.restore(snapshot) || workspace.paneCount() != 4U || workspace.viewCount() != 4U) {
            return {"workspace_churn", {}, false, "workspace snapshot round-trip drifted"};
        }
    }
    BenchmarkResult result;
    result.name = "workspace_churn";
    result.measurements.push_back(nff::bench::measure("iterations", static_cast<std::uint64_t>(iterations)));
    result.measurements.push_back(nff::bench::measure("panes", static_cast<std::uint64_t>(workspace.paneCount())));
    result.measurements.push_back(nff::bench::measure("views", static_cast<std::uint64_t>(workspace.viewCount())));
    return result;
}

[[nodiscard]] std::string viewerPrefix() {
    std::string prefix;
    constexpr std::string_view line = "alpha beta gamma delta epsilon\n";
    while (prefix.size() + line.size() <= 4U * 1024U * 1024U) {
        prefix.append(line);
    }
    prefix.append("nff-stress-viewer-needle\n");
    return prefix;
}

[[nodiscard]] BenchmarkResult viewerNavigationSearch(const std::size_t iterations) {
    TempFixture fixture("nff-stress-viewer");
    const auto path = fixture.path("viewer.txt");
    const auto error = nff::bench::createSparseFile(path, viewerPrefix(), 1U * kGiB);
    if (error) {
        return {"viewer_navigation_search", {}, false, error.message()};
    }
    nff::viewer::LargeFileViewer viewer;
    nff::viewer::ViewerOpenOptions options;
    options.performance = nff::viewer::PerformanceProfile::MemorySaver;
    const auto opened = viewer.open(path, options);
    if (!opened) {
        return {"viewer_navigation_search", {}, false, opened.error.message()};
    }
    nff::search::SearchOptions searchOptions;
    searchOptions.wrapAround = false;
    constexpr std::size_t windowBytes = 64U * 1024U;
    for (std::size_t index = 0U; index < iterations; ++index) {
        const auto mixed = static_cast<std::uint64_t>(index) * 11400714819323198485ULL + 17U;
        const auto offset = mixed % (viewer.size() - windowBytes);
        const auto window = viewer.readRaw(offset, windowBytes);
        if (!window) {
            return {"viewer_navigation_search", {}, false, window.error.message()};
        }
        if ((index % 8U) == 0U) {
            const auto found = viewer.search("nff-stress-viewer-needle", 0U,
                                             nff::search::SearchDirection::Forward,
                                             searchOptions, {});
            if (!found || !found.match.has_value()) {
                return {"viewer_navigation_search", {}, false,
                        found.error ? found.error.message() : "viewer stress search missed needle"};
            }
        }
        if (viewer.cacheStatistics().residentBytes > 20U * 1024U * 1024U) {
            return {"viewer_navigation_search", {}, false, "Viewer MemorySaver cache grew beyond guardrail"};
        }
    }
    BenchmarkResult result;
    result.name = "viewer_navigation_search";
    result.measurements.push_back(nff::bench::measure("iterations", static_cast<std::uint64_t>(iterations)));
    result.measurements.push_back(nff::bench::measure(
        "cache_bytes", static_cast<std::uint64_t>(viewer.cacheStatistics().residentBytes)));
    return result;
}

[[nodiscard]] BenchmarkResult followGrowth(const std::size_t iterations) {
    TempFixture fixture("nff-stress-follow");
    const auto path = fixture.path("follow.log");
    auto error = nff::bench::writeRepeatedText(path, "initial follow line\n", 64U * 1024U);
    if (error) {
        return {"follow_growth", {}, false, error.message()};
    }
    nff::viewer::LargeFileViewer viewer;
    nff::viewer::ViewerOpenOptions options;
    options.performance = nff::viewer::PerformanceProfile::MemorySaver;
    const auto opened = viewer.open(path, options);
    if (!opened) {
        return {"follow_growth", {}, false, opened.error.message()};
    }
    nff::viewer::LiveFileFollower follower(viewer, {20U, 1U * 1024U * 1024U});
    const auto initial = follower.snapshot();
    if (!initial) {
        return {"follow_growth", {}, false, initial.error.message()};
    }
    std::uint64_t previousSize = viewer.size();
    for (std::size_t index = 0U; index < iterations; ++index) {
        const auto marker = "follow-marker-" + std::to_string(index) + "\n";
        {
            std::ofstream output(path, std::ios::binary | std::ios::app);
            output.write(marker.data(), static_cast<std::streamsize>(marker.size()));
            if (!output) {
                return {"follow_growth", {}, false, "unable to append follow marker"};
            }
        }
        const auto update = follower.poll();
        if (!update || update.refresh.kind != nff::viewer::ViewerRefreshKind::Grown ||
            !update.hasTail || update.refresh.currentSize <= previousSize ||
            update.tail.text.find(marker.substr(0U, marker.size() - 1U)) == std::string::npos) {
            return {"follow_growth", {}, false,
                    update.error ? update.error.message() : "follow update lost growth/tail state"};
        }
        previousSize = update.refresh.currentSize;
    }
    BenchmarkResult result;
    result.name = "follow_growth";
    result.measurements.push_back(nff::bench::measure("iterations", static_cast<std::uint64_t>(iterations)));
    result.measurements.push_back(nff::bench::measure("final_bytes", previousSize));
    return result;
}

[[nodiscard]] BenchmarkResult cancelledFileSearch(const std::size_t iterations) {
    TempFixture fixture("nff-stress-file-search");
    constexpr std::size_t directories = 50U;
    constexpr std::size_t filesPerDirectory = 100U;
    for (std::size_t directory = 0U; directory < directories; ++directory) {
        const auto folder = fixture.root() / ("d-" + std::to_string(directory));
        std::error_code error;
        std::filesystem::create_directory(folder, error);
        if (error) {
            return {"cancelled_file_search", {}, false, error.message()};
        }
        for (std::size_t index = 0U; index < filesPerDirectory; ++index) {
            std::ofstream output(folder / ("ordinary-project-component-" + std::to_string(index) + ".txt"));
            if (!output) {
                return {"cancelled_file_search", {}, false, "unable to create search fixture"};
            }
        }
    }
    nff::search::FileSearchIndex searchIndex;
    nff::search::FileSearchBuildOptions options;
    options.includeDirectories = false;
    const std::array<std::filesystem::path, 1> roots{fixture.root()};
    const auto built = searchIndex.rebuild(roots, options);
    if (!built || searchIndex.size() != directories * filesPerDirectory) {
        return {"cancelled_file_search", {}, false,
                built.error ? built.error.message() : "search fixture index size mismatch"};
    }
    std::uint64_t totalChecks = 0U;
    for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
        std::size_t checks = 0U;
        const auto results = searchIndex.search("ordinary project", 100U, [&checks] {
            ++checks;
            return checks >= 2U;
        });
        totalChecks += static_cast<std::uint64_t>(checks);
        if (!results.empty() || checks < 2U) {
            return {"cancelled_file_search", {}, false, "stale file-search cancellation did not abort"};
        }
    }
    BenchmarkResult result;
    result.name = "cancelled_file_search";
    result.measurements.push_back(nff::bench::measure("iterations", static_cast<std::uint64_t>(iterations)));
    result.measurements.push_back(nff::bench::measure("cancel_checks", totalChecks));
    return result;
}

[[nodiscard]] BenchmarkResult optionalIdle() {
    nff::search::FileSearchIndex index;
    nff::storage::FileWatcher watcher;
    nff::app::DocumentFileMonitor monitor;
    nff::viewer::LargeFileViewer viewer;
    const auto cache = viewer.cacheStatistics();
    const bool clean = index.size() == 0U && index.estimatedStorageBytes() == 0U &&
                       watcher.size() == 0U && monitor.size() == 0U && !viewer.isOpen() &&
                       cache.residentBytes == 0U && cache.residentBlocks == 0U &&
                       viewer.lineIndexStatistics().checkpoints == 0U;
    BenchmarkResult result;
    result.name = "optional_idle";
    result.measurements.push_back(nff::bench::measure("file_index_entries", static_cast<std::uint64_t>(index.size())));
    result.measurements.push_back(nff::bench::measure("watchers", static_cast<std::uint64_t>(watcher.size())));
    result.measurements.push_back(nff::bench::measure("document_watches", static_cast<std::uint64_t>(monitor.size())));
    result.measurements.push_back(nff::bench::measure("cache_bytes", static_cast<std::uint64_t>(cache.residentBytes)));
    result.passed = clean;
    if (!result.passed) {
        result.detail = "optional subsystems performed work before explicit activation";
    }
    return result;
}

}

int main(int argc, char** argv) {
    bool quick = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--quick") {
            quick = true;
        } else if (argument == "--help" || argument == "-h") {
            std::cout << "Usage: nff_stress [--quick]\n";
            return 0;
        } else {
            std::cerr << "unknown argument: " << argument << '\n';
            return 2;
        }
    }

    const std::size_t normalIterations = quick ? 24U : 120U;
    const std::size_t heavyIterations = quick ? 12U : 60U;
    const std::vector<nff::bench::BenchmarkCase> cases{
        {"document_open_close", [=] { return documentOpenClose(normalIterations); }},
        {"save_reload_conflict", [=] { return saveReloadConflict(normalIterations); }},
        {"recovery_cycle", [=] { return recoveryCycle(normalIterations); }},
        {"session_restore_cycle", [=] { return sessionRestoreCycle(heavyIterations); }},
        {"workspace_churn", [=] { return workspaceChurn(normalIterations); }},
        {"viewer_navigation_search", [=] { return viewerNavigationSearch(normalIterations); }},
        {"follow_growth", [=] { return followGrowth(heavyIterations); }},
        {"cancelled_file_search", [=] { return cancelledFileSearch(normalIterations); }},
        {"optional_idle", optionalIdle},
    };

    nff::bench::BenchmarkReporter reporter(std::cout, nff::bench::BenchmarkReporter::Format::Human);
    bool failed = false;
    for (const auto& test : cases) {
        try {
            auto result = test.run();
            if (result.name.empty()) {
                result.name = test.name;
            }
            failed = failed || !result.passed;
            reporter.report(result);
        } catch (const std::exception& exception) {
            failed = true;
            reporter.report({test.name, {}, false, exception.what()});
        }
    }
    return failed ? 1 : 0;
}
