#include "BenchmarkSupport.hpp"

#include "notepadFasaFiso/app/PresentationModel.hpp"
#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/metadata/MetadataStore.hpp"
#include "notepadFasaFiso/settings/AppSettings.hpp"
#include "notepadFasaFiso/encoding/TextCodec.hpp"
#include "notepadFasaFiso/search/FileSearch.hpp"
#include "notepadFasaFiso/search/TextSearch.hpp"
#include "notepadFasaFiso/viewer/LargeFileViewer.hpp"
#include "notepadFasaFiso/workspace/WorkspaceModel.hpp"

#include <algorithm>
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
using nff::bench::Stopwatch;
using nff::bench::TempFixture;

constexpr std::uint64_t kMiB = 1024ULL * 1024ULL;
constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;

[[nodiscard]] std::string prefixBlock() {
    const std::string_view line = "0123456789 alpha beta gamma delta epsilon zeta eta theta\n";
    std::string prefix;
    prefix.reserve(4U * 1024U * 1024U);
    while (prefix.size() + line.size() <= 4U * 1024U * 1024U) {
        prefix.append(line);
    }
    return prefix;
}

[[nodiscard]] BenchmarkResult coreStartup() {
    Stopwatch timer;
    std::uint64_t checksum = 0U;
    constexpr std::size_t iterations = 5000U;
    for (std::size_t index = 0U; index < iterations; ++index) {
        nff::core::DocumentManager documents;
        nff::workspace::WorkspaceModel workspace;
        checksum += static_cast<std::uint64_t>(documents.size());
        checksum += static_cast<std::uint64_t>(workspace.paneCount());
    }
    BenchmarkResult result;
    result.name = "core_startup";
    result.measurements.push_back(nff::bench::measure("iterations", static_cast<std::uint64_t>(iterations)));
    result.measurements.push_back(nff::bench::measure("elapsed_us", timer.elapsedMicroseconds()));
    result.measurements.push_back(nff::bench::measure("checksum", checksum));
    result.passed = checksum == static_cast<std::uint64_t>(iterations);
    if (!result.passed) {
        result.detail = "unexpected core construction state";
    }
    return result;
}

[[nodiscard]] BenchmarkResult textOpen1MiB() {
    TempFixture fixture("nff-bench-text-1m");
    const auto path = fixture.path("text-1m.txt");
    const auto writeError = nff::bench::writeRepeatedText(path, "benchmark line alpha beta gamma\n", 1U * kMiB);
    if (writeError) {
        return {"text_open_1m", {}, false, writeError.message()};
    }

    nff::core::DocumentManager documents;
    Stopwatch timer;
    const auto opened = documents.openRouted(path, {});
    const auto elapsed = timer.elapsedMicroseconds();
    const auto* document = opened ? documents.get(opened.id) : nullptr;

    BenchmarkResult result;
    result.name = "text_open_1m";
    result.measurements.push_back(nff::bench::measure("open_us", elapsed));
    result.measurements.push_back(nff::bench::measure(
        "text_bytes", document == nullptr ? 0U : static_cast<std::uint64_t>(document->text().size())));
    result.passed = opened && document != nullptr && document->textBufferLoaded() &&
                    document->profile().recommendedMode == nff::core::OpenMode::Editor &&
                    document->text().size() >= static_cast<std::size_t>(1U * kMiB - 64U);
    if (!result.passed) {
        result.detail = opened ? "1 MiB text was not materialized in Editor mode" : opened.error.message();
    }
    return result;
}

[[nodiscard]] BenchmarkResult textRoute100MiB() {
    TempFixture fixture("nff-bench-text-100m");
    const auto path = fixture.path("text-100m.txt");
    const auto error = nff::bench::createSparseFile(path, prefixBlock(), 100U * kMiB);
    if (error) {
        return {"text_route_100m", {}, false, error.message()};
    }

    nff::core::DocumentManager documents;
    Stopwatch timer;
    const auto opened = documents.openRouted(path, {});
    const auto elapsed = timer.elapsedMicroseconds();
    const auto* document = opened ? documents.get(opened.id) : nullptr;

    BenchmarkResult result;
    result.name = "text_route_100m";
    result.measurements.push_back(nff::bench::measure("route_us", elapsed));
    result.measurements.push_back(nff::bench::measure(
        "file_bytes", document == nullptr ? 0U : static_cast<std::uint64_t>(document->profile().fileSize)));
    result.passed = opened && document != nullptr && !document->textBufferLoaded() &&
                    document->profile().recommendedMode == nff::core::OpenMode::Viewer &&
                    document->profile().fileSize == 100U * kMiB;
    if (!result.passed) {
        result.detail = opened ? "100 MiB text did not route to scalable Viewer reference"
                               : opened.error.message();
    }
    return result;
}

[[nodiscard]] BenchmarkResult sparseViewer(const std::string& name, const std::uint64_t fileBytes) {
    TempFixture fixture("nff-bench-" + name);
    const auto path = fixture.path(name + ".txt");
    const auto error = nff::bench::createSparseFile(path, prefixBlock(), fileBytes);
    if (error) {
        return {name, {}, false, error.message()};
    }

    nff::viewer::LargeFileViewer viewer;
    nff::viewer::ViewerOpenOptions options;
    options.performance = nff::viewer::PerformanceProfile::MemorySaver;
    Stopwatch openTimer;
    const auto opened = viewer.open(path, options);
    const auto openUs = openTimer.elapsedMicroseconds();
    if (!opened) {
        return {name, {nff::bench::measure("open_us", openUs)}, false, opened.error.message()};
    }

    constexpr std::size_t readBytes = 64U * 1024U;
    constexpr std::uint64_t reads = 256U;
    std::uint64_t bytesRead = 0U;
    Stopwatch readTimer;
    for (std::uint64_t index = 0U; index < reads; ++index) {
        const auto range = fileBytes > readBytes ? fileBytes - static_cast<std::uint64_t>(readBytes) : 0U;
        const auto mixed = index * 11400714819323198485ULL + 0x9E3779B97F4A7C15ULL;
        const auto offset = range == 0U ? 0U : mixed % range;
        const auto window = viewer.readRaw(offset, readBytes);
        if (!window) {
            return {name, {}, false, window.error.message()};
        }
        bytesRead += static_cast<std::uint64_t>(window.bytes.size());
    }
    const auto readUs = readTimer.elapsedMicroseconds();
    const auto cache = viewer.cacheStatistics();

    BenchmarkResult result;
    result.name = name;
    result.measurements.push_back(nff::bench::measure("file_bytes", fileBytes));
    result.measurements.push_back(nff::bench::measure("open_us", openUs));
    result.measurements.push_back(nff::bench::measure("random_reads", reads));
    result.measurements.push_back(nff::bench::measure("read_us", readUs));
    result.measurements.push_back(nff::bench::measure("bytes_read", bytesRead));
    result.measurements.push_back(nff::bench::measure(
        "cache_bytes", static_cast<std::uint64_t>(cache.residentBytes)));
    result.measurements.push_back(nff::bench::measure("cache_hits", cache.hits));
    result.measurements.push_back(nff::bench::measure("cache_misses", cache.misses));
    result.passed = viewer.size() == fileBytes && bytesRead != 0U &&
                    cache.residentBytes <= 20U * 1024U * 1024U;
    if (!result.passed) {
        result.detail = "scalable Viewer exceeded loose 20 MiB MemorySaver cache guardrail";
    }
    return result;
}

[[nodiscard]] BenchmarkResult viewerLineIndex() {
    TempFixture fixture("nff-bench-line-index");
    const auto path = fixture.path("lines.txt");
    constexpr std::uint64_t bytes = 16U * kMiB;
    const auto error = nff::bench::writeRepeatedText(path, "0123456789abcdef\n", bytes);
    if (error) {
        return {"viewer_line_index", {}, false, error.message()};
    }

    nff::viewer::LargeFileViewer viewer;
    nff::viewer::ViewerOpenOptions options;
    options.performance = nff::viewer::PerformanceProfile::MemorySaver;
    const auto opened = viewer.open(path, options);
    if (!opened) {
        return {"viewer_line_index", {}, false, opened.error.message()};
    }

    Stopwatch timer;
    const auto target = viewer.lineStart(250000U);
    const auto counted = viewer.lineCount();
    const auto elapsed = timer.elapsedMicroseconds();
    const auto statistics = viewer.lineIndexStatistics();

    BenchmarkResult result;
    result.name = "viewer_line_index";
    result.measurements.push_back(nff::bench::measure("index_us", elapsed));
    result.measurements.push_back(nff::bench::measure("lines", counted.lines));
    result.measurements.push_back(nff::bench::measure(
        "checkpoints", static_cast<std::uint64_t>(statistics.checkpoints)));
    result.measurements.push_back(nff::bench::measure("indexed_bytes", statistics.furthestIndexedOffset));
    result.passed = target && target.offset.has_value() && counted && counted.lines > 250000U &&
                    statistics.furthestIndexedOffset > 0U && statistics.checkpoints > 0U;
    if (!result.passed) {
        result.detail = "line index failed to advance monotonically across a bounded fixture";
    }
    return result;
}

[[nodiscard]] BenchmarkResult viewerLiteralSearch() {
    TempFixture fixture("nff-bench-search");
    const auto path = fixture.path("search.txt");
    constexpr std::string_view needle = "unique-nff-benchmark-needle";
    const std::string tail = std::string("\n") + std::string(needle) + "\n";
    const auto error = nff::bench::writeRepeatedText(
        path, "alpha beta gamma delta epsilon\n", 24U * kMiB, tail);
    if (error) {
        return {"viewer_literal_search", {}, false, error.message()};
    }

    nff::viewer::LargeFileViewer viewer;
    nff::viewer::ViewerOpenOptions options;
    options.performance = nff::viewer::PerformanceProfile::MemorySaver;
    const auto opened = viewer.open(path, options);
    if (!opened) {
        return {"viewer_literal_search", {}, false, opened.error.message()};
    }

    nff::search::SearchOptions searchOptions;
    searchOptions.wrapAround = false;
    nff::search::StreamingSearchOptions streaming;
    streaming.chunkBytes = 1U * 1024U * 1024U;
    Stopwatch timer;
    const auto found = viewer.search(needle, 0U, nff::search::SearchDirection::Forward,
                                     searchOptions, streaming);
    const auto elapsed = timer.elapsedMicroseconds();

    BenchmarkResult result;
    result.name = "viewer_literal_search";
    result.measurements.push_back(nff::bench::measure("search_us", elapsed));
    result.measurements.push_back(nff::bench::measure("bytes_scanned", found.bytesScanned));
    result.measurements.push_back(nff::bench::measure(
        "match_offset", found.match.has_value() ? found.match->offset : 0U));
    result.passed = found && found.match.has_value() && found.bytesScanned > 0U;
    if (!result.passed) {
        result.detail = found.error ? found.error.message() : "literal needle not found";
    }
    return result;
}

[[nodiscard]] BenchmarkResult viewerUtf16Utf32Search() {
    TempFixture fixture("nff-bench-utf-search");
    constexpr std::string_view needle = "nff-utf-search-needle";
    std::string text;
    text.reserve(2U * 1024U * 1024U);
    while (text.size() < 1024U * 1024U) {
        text.append("unicode search payload alpha beta gamma\n");
    }
    text.append(needle);
    text.push_back('\n');

    struct Case final {
        const char* filename;
        nff::encoding::Encoding encoding;
    };
    const std::array<Case, 2> cases{{
        {"utf16le.txt", nff::encoding::Encoding::Utf16LE},
        {"utf32be.txt", nff::encoding::Encoding::Utf32BE},
    }};

    std::uint64_t totalUs = 0U;
    std::uint64_t totalScanned = 0U;
    std::uint64_t matches = 0U;
    for (const auto& item : cases) {
        const auto encoded = nff::encoding::TextCodec::encode(text, item.encoding, true);
        if (!encoded) {
            return {"viewer_utf16_utf32_search", {}, false, encoded.error.message()};
        }
        const auto path = fixture.path(item.filename);
        const auto writeError = nff::bench::writeBytes(path, encoded.bytes);
        if (writeError) {
            return {"viewer_utf16_utf32_search", {}, false, writeError.message()};
        }
        nff::viewer::LargeFileViewer viewer;
        nff::viewer::ViewerOpenOptions options;
        options.performance = nff::viewer::PerformanceProfile::MemorySaver;
        const auto opened = viewer.open(path, options);
        if (!opened) {
            return {"viewer_utf16_utf32_search", {}, false, opened.error.message()};
        }
        nff::search::SearchOptions searchOptions;
        searchOptions.wrapAround = false;
        Stopwatch timer;
        const auto found = viewer.search(needle, 0U, nff::search::SearchDirection::Forward,
                                         searchOptions, {});
        totalUs += timer.elapsedMicroseconds();
        if (!found || !found.match.has_value()) {
            return {"viewer_utf16_utf32_search", {}, false,
                    found.error ? found.error.message() : "encoded needle not found"};
        }
        totalScanned += found.bytesScanned;
        ++matches;
    }

    BenchmarkResult result;
    result.name = "viewer_utf16_utf32_search";
    result.measurements.push_back(nff::bench::measure("encodings", matches));
    result.measurements.push_back(nff::bench::measure("search_us", totalUs));
    result.measurements.push_back(nff::bench::measure("bytes_scanned", totalScanned));
    result.passed = matches == cases.size() && totalScanned > 0U;
    return result;
}

[[nodiscard]] BenchmarkResult documentNewlineEdit() {
    constexpr std::size_t targetBytes = 32U * 1024U * 1024U;
    constexpr std::string_view line = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abc\n";
    std::string text;
    text.reserve(targetBytes + line.size());
    while (text.size() < targetBytes) text.append(line);

    nff::core::Document document;
    document.replaceText(std::move(text));
    const auto offset = document.text().size() / 2U;
    constexpr std::size_t editPairs = 16U;
    Stopwatch timer;
    for (std::size_t index = 0U; index < editPairs; ++index) {
        const auto insertError = document.applyEdit(offset, 0U, "\n");
        const auto eraseError = document.applyEdit(offset, 1U, "");
        if (insertError || eraseError) {
            return {"document_newline_edit", {}, false,
                    insertError ? insertError.message() : eraseError.message()};
        }
    }
    BenchmarkResult result;
    result.name = "document_newline_edit";
    result.measurements.push_back(nff::bench::measure("document_bytes", static_cast<std::uint64_t>(document.text().size())));
    result.measurements.push_back(nff::bench::measure("edit_pairs", static_cast<std::uint64_t>(editPairs)));
    result.measurements.push_back(nff::bench::measure("elapsed_us", timer.elapsedMicroseconds()));
    result.measurements.push_back(nff::bench::measure("revision", document.revision()));
    result.passed = document.profile().lineEnding == nff::core::LineEnding::LF &&
                    document.profile().longestSampledLine != 0U;
    return result;
}

[[nodiscard]] BenchmarkResult appearanceSparseEdit() {
    nff::metadata::TextAppearanceMap appearance;
    constexpr std::uint64_t spanCount = 10000U;
    for (std::uint64_t index = 0U; index < spanCount; ++index) {
        const auto begin = index * 4U;
        appearance.setForeground(begin, begin + 2U,
                                 index % 2U == 0U ? 0xFF112233U : 0xFF334455U);
    }
    constexpr std::size_t editPairs = 64U;
    Stopwatch timer;
    for (std::size_t index = 0U; index < editPairs; ++index) {
        appearance.applyEdit(20001U, 0U, 1U);
        appearance.applyEdit(20001U, 1U, 0U);
    }
    BenchmarkResult result;
    result.name = "appearance_sparse_edit";
    result.measurements.push_back(nff::bench::measure("spans", static_cast<std::uint64_t>(appearance.spans().size())));
    result.measurements.push_back(nff::bench::measure("edit_pairs", static_cast<std::uint64_t>(editPairs)));
    result.measurements.push_back(nff::bench::measure("elapsed_us", timer.elapsedMicroseconds()));
    result.passed = appearance.spans().size() == spanCount;
    return result;
}

[[nodiscard]] BenchmarkResult presentationSync() {
    nff::core::DocumentManager documents;
    nff::workspace::WorkspaceModel workspace;
    nff::settings::AppSettings settings;
    const auto document = documents.createUntitled();
    constexpr std::size_t viewCount = 256U;
    for (std::size_t index = 0U; index < viewCount; ++index) {
        static_cast<void>(workspace.openView(document, workspace.primaryPane()));
    }
    nff::app::PresentationModel presentation(documents, workspace, settings);
    constexpr std::size_t iterations = 2000U;
    Stopwatch timer;
    for (std::size_t index = 0U; index < iterations; ++index) presentation.sync();
    BenchmarkResult result;
    result.name = "presentation_sync";
    result.measurements.push_back(nff::bench::measure("views", static_cast<std::uint64_t>(workspace.viewCount())));
    result.measurements.push_back(nff::bench::measure("iterations", static_cast<std::uint64_t>(iterations)));
    result.measurements.push_back(nff::bench::measure("elapsed_us", timer.elapsedMicroseconds()));
    result.passed = workspace.viewCount() == viewCount;
    return result;
}

[[nodiscard]] BenchmarkResult presentationSnapshot() {
    nff::core::DocumentManager documents;
    nff::workspace::WorkspaceModel workspace;
    nff::settings::AppSettings settings;
    const auto document = documents.createUntitled();
    constexpr std::size_t viewCount = 256U;
    for (std::size_t index = 0U; index < viewCount; ++index) {
        static_cast<void>(workspace.openView(document, workspace.primaryPane()));
    }
    nff::app::PresentationModel presentation(documents, workspace, settings);
    constexpr std::size_t iterations = 1000U;
    std::uint64_t checksum = 0U;
    Stopwatch timer;
    for (std::size_t index = 0U; index < iterations; ++index) {
        const auto snapshot = presentation.snapshot();
        checksum += static_cast<std::uint64_t>(snapshot.panes.size());
        if (!snapshot.panes.empty()) checksum += static_cast<std::uint64_t>(snapshot.panes.front().tabs.size());
    }
    BenchmarkResult result;
    result.name = "presentation_snapshot";
    result.measurements.push_back(nff::bench::measure("views", static_cast<std::uint64_t>(workspace.viewCount())));
    result.measurements.push_back(nff::bench::measure("iterations", static_cast<std::uint64_t>(iterations)));
    result.measurements.push_back(nff::bench::measure("checksum", checksum));
    result.measurements.push_back(nff::bench::measure("elapsed_us", timer.elapsedMicroseconds()));
    result.passed = checksum == static_cast<std::uint64_t>(iterations) * (viewCount + 1U);
    return result;
}

[[nodiscard]] BenchmarkResult sidebar50k() {
    TempFixture fixture("nff-bench-sidebar-50k");
    constexpr std::size_t directoryCount = 100U;
    constexpr std::size_t filesPerDirectory = 500U;
    constexpr std::size_t totalFiles = directoryCount * filesPerDirectory;

    Stopwatch fixtureTimer;
    for (std::size_t directory = 0U; directory < directoryCount; ++directory) {
        const auto folder = fixture.root() / ("dir-" + std::to_string(directory));
        std::error_code error;
        std::filesystem::create_directory(folder, error);
        if (error) {
            return {"sidebar_50k", {}, false, error.message()};
        }
        for (std::size_t index = 0U; index < filesPerDirectory; ++index) {
            const bool target = directory == directoryCount - 1U && index == filesPerDirectory - 1U;
            const auto filename = target
                                      ? std::string("needle-target-final-file.txt")
                                      : "ordinary-project-component-" + std::to_string(directory) + "-" +
                                            std::to_string(index) + "-alpha-beta-gamma.txt";
            std::ofstream output(folder / filename, std::ios::binary);
            if (!output) {
                return {"sidebar_50k", {}, false, "unable to create sidebar fixture"};
            }
        }
    }
    const auto fixtureUs = fixtureTimer.elapsedMicroseconds();

    nff::search::FileSearchBuildOptions buildOptions;
    buildOptions.includeDirectories = false;
    buildOptions.maxEntries = 100000U;
    nff::search::FileSearchIndex index;
    const std::array<std::filesystem::path, 1> roots{fixture.root()};
    Stopwatch buildTimer;
    const auto built = index.rebuild(roots, buildOptions);
    const auto buildUs = buildTimer.elapsedMicroseconds();
    if (!built) {
        return {"sidebar_50k", {}, false, built.error.message()};
    }

    Stopwatch searchTimer;
    const auto hits = index.search("needle target", 100U);
    const auto searchUs = searchTimer.elapsedMicroseconds();
    std::size_t cancellationChecks = 0U;
    Stopwatch cancelTimer;
    const auto cancelled = index.search("ordinary project", 100U, [&cancellationChecks] {
        ++cancellationChecks;
        return cancellationChecks >= 2U;
    });
    const auto cancelUs = cancelTimer.elapsedMicroseconds();

    BenchmarkResult result;
    result.name = "sidebar_50k";
    result.measurements.push_back(nff::bench::measure("fixture_us", fixtureUs));
    result.measurements.push_back(nff::bench::measure("build_us", buildUs));
    result.measurements.push_back(nff::bench::measure("search_us", searchUs));
    result.measurements.push_back(nff::bench::measure("cancel_us", cancelUs));
    result.measurements.push_back(nff::bench::measure(
        "entries", static_cast<std::uint64_t>(index.size())));
    result.measurements.push_back(nff::bench::measure(
        "storage_bytes", static_cast<std::uint64_t>(index.estimatedStorageBytes())));
    result.measurements.push_back(nff::bench::measure(
        "cancel_checks", static_cast<std::uint64_t>(cancellationChecks)));
    result.passed = built.stats.indexedFiles == totalFiles && index.size() == totalFiles &&
                    hits.size() == 1U && cancelled.empty() && cancellationChecks >= 2U;
    if (!result.passed) {
        result.detail = "50k file-search fixture failed indexing/search/cancellation invariants";
    }
    return result;
}

}

int main(const int argc, char** argv) {
    const std::vector<nff::bench::BenchmarkCase> cases{
        {"core_startup", coreStartup},
        {"text_open_1m", textOpen1MiB},
        {"text_route_100m", textRoute100MiB},
        {"viewer_sparse_1g", [] { return sparseViewer("viewer_sparse_1g", 1U * kGiB); }},
        {"viewer_sparse_8g", [] { return sparseViewer("viewer_sparse_8g", 8U * kGiB); }},
        {"viewer_line_index", viewerLineIndex},
        {"viewer_literal_search", viewerLiteralSearch},
        {"viewer_utf16_utf32_search", viewerUtf16Utf32Search},
        {"document_newline_edit", documentNewlineEdit},
        {"appearance_sparse_edit", appearanceSparseEdit},
        {"presentation_sync", presentationSync},
        {"presentation_snapshot", presentationSnapshot},
        {"sidebar_50k", sidebar50k},
    };
    return nff::bench::runBenchmarkProgram(argc, argv, cases, std::cout, std::cerr);
}
