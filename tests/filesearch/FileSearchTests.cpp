#include "notepadFasaFiso/search/FileSearch.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void writeFile(const std::filesystem::path& path, const std::string_view text = "x") {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void testBuildSearchAndRanking() {
    const auto root = std::filesystem::temp_directory_path() / "nff-file-search";
    std::error_code error;
    std::filesystem::remove_all(root, error);

    writeFile(root / "notes.txt");
    writeFile(root / "src" / "main.cpp");
    writeFile(root / "src" / "main_window.cpp");
    writeFile(root / "docs" / "main-notes.md");
    writeFile(root / "Dev" / "notepadFasaFiso" / "nff-conflict-test.txt");
    writeFile(root / ".hidden" / "secret.txt");
    writeFile(root / ".git" / "objects" / "ignored");
    writeFile(root / "$Recycle.Bin" / "deleted.txt");
    writeFile(root / "System Volume Information" / "metadata.txt");

    nff::search::FileSearchIndex index;
    const std::filesystem::path roots[]{root};
    const auto built = index.rebuild(roots);
    require(static_cast<bool>(built), "file index build succeeds");
    require(built.stats.indexedFiles == 5U,
            "hidden, VCS, recycle-bin, and system-volume files are skipped");
    require(index.size() >= 6U, "directories are indexed by default");

    auto hits = index.search("main", 10U);
    require(hits.size() == 3U, "main query returns matching files");
    require(hits.front().path.filename() == "main.cpp", "exact filename stem ranks highly");

    hits = index.search("src main", 10U);
    require(hits.size() == 2U, "multi-token query requires all tokens");

    hits = index.search("mnwn", 10U);
    require(!hits.empty() && hits.front().path.filename() == "main_window.cpp",
            "subsequence fuzzy match works");

    hits = index.search("a", 3U);
    require(hits.size() == 3U,
            "broad single-character query returns bounded matches");

    hits = index.search("nff-conflict-test", 10U);
    require(hits.size() == 1U && hits.front().path.filename() == "nff-conflict-test.txt",
            "hyphenated nested filename is searchable");
    require(hits.front().relativePathUtf8 == "Dev/notepadFasaFiso/nff-conflict-test.txt",
            "search hits preserve the index-owned root-relative path");

    std::filesystem::remove_all(root, error);
}

void testIncrementalUpdateAndCancellation() {
    const auto root = std::filesystem::temp_directory_path() / "nff-file-search-update";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    writeFile(root / "alpha.txt");

    nff::search::FileSearchIndex index;
    const std::filesystem::path roots[]{root};
    require(static_cast<bool>(index.rebuild(roots)), "initial update index build");
    const auto generation = index.generation();

    writeFile(root / "beta.log");
    require(index.upsert(root / "beta.log", root), "upsert new file");
    require(index.generation() > generation, "upsert increments generation");
    require(index.search("beta").size() == 1U, "upserted file is searchable");

    require(index.erase(root / "beta.log"), "erase indexed file");
    require(index.search("beta").empty(), "erased file disappears from search");

    writeFile(root / "nested" / "one.txt");
    writeFile(root / "nested" / "deep" / "two.txt");
    require(index.upsert(root / "nested" / "one.txt", root), "upsert subtree file");
    require(index.upsert(root / "nested" / "deep" / "two.txt", root), "upsert deep subtree file");
    require(index.eraseSubtree(root / "nested") == 2U, "erase subtree removes descendants");
    require(index.search("one").empty() && index.search("two").empty(),
            "erased subtree disappears from search");

    bool cancel = true;
    const auto cancelled = index.rebuild(roots, {}, [&cancel] { return cancel; });
    require(!static_cast<bool>(cancelled), "cancelled build reports error");
    require(cancelled.stats.cancelled, "cancelled build reports cancellation state");
    require(cancelled.error == std::make_error_code(std::errc::operation_canceled),
            "cancelled build error code");

    std::filesystem::remove_all(root, error);
}

void testCompactStorageAndRelease() {
    const auto root = std::filesystem::temp_directory_path() / "nff-file-search-compact";
    std::error_code error;
    std::filesystem::remove_all(root, error);

    constexpr std::size_t fileCount = 1'000U;
    for (std::size_t index = 0U; index < fileCount; ++index) {
        writeFile(root / "src" /
                  ("long-component-for-index-memory-test-" + std::to_string(index) + ".txt"));
    }

    nff::search::FileSearchBuildOptions options;
    options.includeDirectories = false;
    nff::search::FileSearchIndex index;
    const std::filesystem::path roots[]{root};
    const auto built = index.rebuild(roots, options);
    require(static_cast<bool>(built), "compact index build succeeds");
    require(index.size() == fileCount, "compact index contains all files");
    require(index.estimatedStorageBytes() < 512U * 1024U,
            "compact index avoids per-entry path/string duplication");

    std::size_t cancellationChecks = 0U;
    const auto cancelledHits = index.search(
        "long-component", 100U, [&cancellationChecks] {
            ++cancellationChecks;
            return cancellationChecks >= 2U;
        });
    require(cancelledHits.empty(), "cancelled search does not publish partial results");
    require(cancellationChecks >= 2U,
            "large index search periodically observes cancellation requests");

    const auto storageBeforeUpsert = index.estimatedStorageBytes();
    const auto repeatedPath = root / "src" / "long-component-for-index-memory-test-0.txt";
    for (std::size_t iteration = 0U; iteration < 64U; ++iteration) {
        require(index.upsert(repeatedPath, root), "repeated upsert succeeds");
    }
    require(index.estimatedStorageBytes() == storageBeforeUpsert,
            "repeated upsert reuses compact path storage");

    index.clear();
    require(index.size() == 0U, "clear removes compact index entries");
    require(index.estimatedStorageBytes() == 0U,
            "clear releases compact index backing storage");

    std::filesystem::remove_all(root, error);
}

void testBreadthFirstBudgetFairness() {
    const auto base = std::filesystem::temp_directory_path() / "nff-file-search-bfs";
    const auto targetRoot = base / "target-root";
    const auto heavyRoot = base / "heavy-root";
    std::error_code error;
    std::filesystem::remove_all(base, error);

    writeFile(targetRoot / "Dev" / "notepadFasaFiso" / "must-be-found.txt");
    for (int directory = 0; directory < 6; ++directory) {
        for (int file = 0; file < 8; ++file) {
            writeFile(heavyRoot / "Windows" / ("deep-" + std::to_string(directory)) /
                      ("system-" + std::to_string(file) + ".txt"));
        }
    }

    nff::search::FileSearchBuildOptions options;
    options.includeDirectories = false;
    options.maxEntries = 5U;

    nff::search::FileSearchIndex index;

    const std::filesystem::path roots[]{targetRoot, heavyRoot};
    const auto built = index.rebuild(roots, options);
    require(static_cast<bool>(built), "breadth-first limited build succeeds");
    require(built.stats.truncated, "breadth-first fairness fixture reaches entry limit");
    const auto hits = index.search("must-be-found", 10U);
    require(!hits.empty(), "entry budget does not starve an earlier sibling/root");

    std::filesystem::remove_all(base, error);
}

void testFollowDirectorySymlinkSafely() {
    const auto base = std::filesystem::temp_directory_path() / "nff-file-search-links";
    const auto root = base / "root";
    const auto outside = base / "outside";
    std::error_code error;
    std::filesystem::remove_all(base, error);
    writeFile(outside / "linked-file.txt");
    std::filesystem::create_directories(root, error);
    require(!error, "create link-search root");
    error.clear();
    std::filesystem::create_directory_symlink(outside, root / "DevLink", error);
    if (error) {

        std::filesystem::remove_all(base, error);
        return;
    }

    nff::search::FileSearchBuildOptions options;
    options.includeDirectories = false;
    options.followDirectorySymlinks = true;
    nff::search::FileSearchIndex index;
    const std::filesystem::path roots[]{root};
    const auto built = index.rebuild(roots, options);
    require(static_cast<bool>(built), "symlink-follow build succeeds");
    const auto hits = index.search("linked-file", 10U);
    require(!hits.empty(), "followed directory symlink is searchable");
    if (!hits.empty()) {
        require(hits.front().relativePathUtf8.find("DevLink") != std::string::npos,
                "followed link keeps the visible tree namespace");
    }

    std::filesystem::remove_all(base, error);
}

void testEntryLimit() {
    const auto root = std::filesystem::temp_directory_path() / "nff-file-search-limit";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    for (int index = 0; index < 20; ++index) {
        writeFile(root / ("file-" + std::to_string(index) + ".txt"));
    }

    nff::search::FileSearchBuildOptions options;
    options.includeDirectories = false;
    options.maxEntries = 5U;

    nff::search::FileSearchIndex index;
    const std::filesystem::path roots[]{root};
    const auto built = index.rebuild(roots, options);
    require(static_cast<bool>(built), "limited build succeeds");
    require(built.stats.truncated, "entry limit reports truncation");
    require(index.size() == 5U, "entry limit is enforced");

    std::filesystem::remove_all(root, error);
}

}

int main() {
    testBuildSearchAndRanking();
    testIncrementalUpdateAndCancellation();
    testEntryLimit();
    testBreadthFirstBudgetFairness();
    testFollowDirectorySymlinkSafely();
    testCompactStorageAndRelease();
    return EXIT_SUCCESS;
}
