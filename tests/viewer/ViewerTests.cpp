#include "notepadFasaFiso/encoding/TextCodec.hpp"
#include "notepadFasaFiso/storage/FileWriter.hpp"
#include "notepadFasaFiso/storage/RandomAccessFile.hpp"
#include "notepadFasaFiso/viewer/LargeFileViewer.hpp"
#include "notepadFasaFiso/viewer/LiveFileFollower.hpp"
#include "notepadFasaFiso/viewer/TextPresentation.hpp"
#include "notepadFasaFiso/viewer/LineIndex.hpp"
#include "notepadFasaFiso/viewer/ViewCache.hpp"

#include <array>
#include <concepts>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] std::filesystem::path tempRoot(const std::string_view name) {
    auto path = std::filesystem::temp_directory_path() / std::string(name);
    std::error_code error;
    std::filesystem::remove_all(path, error);
    error.clear();
    std::filesystem::create_directories(path, error);
    require(!error, "create temp directory");
    return path;
}

template <typename Viewer>
constexpr bool hasPreparedOpen = requires(
    Viewer& viewer,
    const std::filesystem::path& path,
    const nff::core::DocumentProfile& profile,
    const nff::storage::FileState& state,
    const nff::viewer::ViewerOpenOptions& options) {
    { viewer.openPrepared(path, profile, state, options) } ->
        std::same_as<nff::viewer::ViewerOpenResult>;
};

void testPreparedViewerOpenApiExists() {
    require(hasPreparedOpen<nff::viewer::LargeFileViewer>,
            "large viewer exposes prepared open path for already-inspected documents");
}

[[nodiscard]] std::vector<std::byte> asBytes(const std::string_view text) {
    const auto* first = reinterpret_cast<const std::byte*>(text.data());
    return {first, first + text.size()};
}

void testPreparedViewerOpenReusesStableInspection() {
    const auto root = tempRoot("nff-viewer-prepared-open");
    const auto path = root / "prepared.txt";
    const std::string content = "alpha\nbeta\ngamma\n";
    auto error = nff::storage::FileWriter::writeAtomically(path, asBytes(content));
    require(!error, "write prepared-open fixture");

    nff::core::InspectOptions inspectOptions;
    inspectOptions.viewerSizeThreshold = 1U;
    const auto inspected = nff::core::FileSniffer::inspect(path, inspectOptions);
    require(static_cast<bool>(inspected), "inspect prepared-open fixture");
    const auto state = nff::storage::FileStateTracker::capture(path);
    require(static_cast<bool>(state) && state.state.exists,
            "capture prepared-open file state");

    auto preparedProfile = inspected.profile;
    preparedProfile.longestSampledLine = 424242U;

    nff::viewer::ViewerOpenOptions options;
    options.inspectOptions = inspectOptions;
    nff::viewer::LargeFileViewer viewer;
    const auto opened = viewer.openPrepared(path, preparedProfile, state.state, options);
    require(static_cast<bool>(opened), "open viewer from stable prepared inspection");
    require(viewer.profile().longestSampledLine == 424242U,
            "prepared viewer reuses the supplied inspected profile");

    std::filesystem::remove_all(root, error);
}

void testPreparedViewerOpenFallsBackWhenFileChanged() {
    const auto root = tempRoot("nff-viewer-prepared-stale");
    const auto path = root / "prepared-stale.txt";
    const std::string original = "alpha\nbeta\n";
    auto error = nff::storage::FileWriter::writeAtomically(path, asBytes(original));
    require(!error, "write stale prepared-open fixture");

    nff::core::InspectOptions inspectOptions;
    inspectOptions.viewerSizeThreshold = 1U;
    const auto inspected = nff::core::FileSniffer::inspect(path, inspectOptions);
    const auto state = nff::storage::FileStateTracker::capture(path);
    require(static_cast<bool>(inspected) && static_cast<bool>(state),
            "capture stale prepared-open metadata");

    auto staleProfile = inspected.profile;
    staleProfile.longestSampledLine = 424242U;
    const std::string changed = original + "gamma\n";
    error = nff::storage::FileWriter::writeAtomically(path, asBytes(changed));
    require(!error, "change prepared-open fixture after inspection");

    nff::viewer::ViewerOpenOptions options;
    options.inspectOptions = inspectOptions;
    nff::viewer::LargeFileViewer viewer;
    const auto opened = viewer.openPrepared(path, staleProfile, state.state, options);
    require(static_cast<bool>(opened), "stale prepared viewer falls back to normal inspection");
    require(viewer.profile().fileSize == changed.size(),
            "fallback inspection observes changed file size");
    require(viewer.profile().longestSampledLine != 424242U,
            "fallback inspection discards stale prepared profile");

    std::filesystem::remove_all(root, error);
}

void testRandomAccessAndCache() {
    const auto root = tempRoot("nff-viewer-random");
    const auto path = root / "large.txt";
    std::string content;
    content.reserve(512U * 1024U);
    for (std::size_t index = 0U; index < 32768U; ++index) {
        content += "0123456789abcdef\n";
    }
    auto error = nff::storage::FileWriter::writeAtomically(path, asBytes(content));
    require(!error, "write random access fixture");

    nff::storage::RandomAccessFile file;
    error = file.open(path);
    require(!error, "open random access file");
    require(file.size() == content.size(), "random access size");

    std::array<std::byte, 16> bytes{};
    auto read = file.readAt(17U, bytes);
    require(static_cast<bool>(read) && read.bytesRead == bytes.size(), "random read");
    require(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()) ==
                content.substr(17U, bytes.size()),
            "random read content");

    nff::viewer::CachePolicy policy;
    policy.blockBytes = 4096U;
    policy.maximumBytes = 8192U;
    policy.prefetchBlocks = 0U;
    nff::viewer::ViewCache cache(policy);

    std::array<std::byte, 6000> cached{};
    auto cachedRead = cache.read(file, 2048U, cached);
    require(static_cast<bool>(cachedRead) && cachedRead.bytesRead == cached.size(),
            "cache multi-block read");
    const auto firstStats = cache.statistics();
    require(firstStats.residentBytes <= policy.maximumBytes, "cache respects memory budget");

    cachedRead = cache.read(file, 2048U, cached);
    require(static_cast<bool>(cachedRead), "cache repeat read");
    require(cache.statistics().hits > firstStats.hits, "cache records hit");

    std::filesystem::remove_all(root, error);
}

void testLineIndexMixedEndings() {
    const auto root = tempRoot("nff-viewer-lines");
    const auto path = root / "mixed.txt";
    const std::string content = "one\r\ntwo\nthree\rfour";
    auto error = nff::storage::FileWriter::writeAtomically(path, asBytes(content));
    require(!error, "write line fixture");

    nff::storage::RandomAccessFile file;
    error = file.open(path);
    require(!error, "open line fixture");

    nff::viewer::CachePolicy policy{4U * 1024U, 8U * 1024U, 0U};
    nff::viewer::ViewCache cache(policy);
    nff::viewer::LineIndex index;
    nff::viewer::LineIndexOptions indexOptions;
    indexOptions.checkpointStrideLines = 2U;
    indexOptions.scanChunkBytes = 4096U;
    index.reset(nff::encoding::Encoding::Utf8, 0U, file.size(), indexOptions);

    const auto line2 = index.lineStart(file, cache, 2U);
    require(static_cast<bool>(line2) && line2.offset == 5U, "CRLF line start");
    const auto line3 = index.lineStart(file, cache, 3U);
    require(static_cast<bool>(line3) && line3.offset == 9U, "LF line start");
    const auto line4 = index.lineStart(file, cache, 4U);
    require(static_cast<bool>(line4) && line4.offset == 15U, "CR line start");

    const auto atOffset = index.lineAtOffset(file, cache, 12U);
    require(static_cast<bool>(atOffset) && atOffset.line == 3U, "line at byte offset");
    const auto count = index.lineCount(file, cache);
    require(static_cast<bool>(count) && count.lines == 4U, "mixed ending line count");

    std::filesystem::remove_all(root, error);
}

void testUtf16LineIndexAndViewer() {
    const auto root = tempRoot("nff-viewer-utf16");
    const auto path = root / "utf16.txt";
    constexpr std::string_view content = "bir\r\niki\nüç\r\ndört";
    const auto encoded = nff::encoding::TextCodec::encode(
        content, nff::encoding::Encoding::Utf16LE, true);
    require(static_cast<bool>(encoded), "encode UTF-16 viewer fixture");
    auto error = nff::storage::FileWriter::writeAtomically(path, encoded.bytes);
    require(!error, "write UTF-16 viewer fixture");

    nff::viewer::LargeFileViewer viewer;
    nff::viewer::ViewerOpenOptions options;
    options.performance = nff::viewer::PerformanceProfile::MemorySaver;
    const auto opened = viewer.open(path, options);
    require(static_cast<bool>(opened), "open UTF-16 viewer");
    require(viewer.performanceProfile() == nff::viewer::PerformanceProfile::MemorySaver,
            "explicit memory saver profile");
    require(viewer.byteSearchCompatible(), "UTF-16 viewer exposes bounded encoded literal search");

    const auto line2 = viewer.lineStart(2U);
    require(static_cast<bool>(line2) && line2.offset.has_value(), "UTF-16 second line indexed");
    const auto window = viewer.readLines(2U, 2U);
    require(static_cast<bool>(window), "read UTF-16 line window");
    require(window.text == "iki\nüç\r\n", "UTF-16 line window decoded to canonical UTF-8");

    const auto count = viewer.lineCount();
    require(static_cast<bool>(count) && count.lines == 4U, "UTF-16 line count");

    const auto emojiPath = root / "utf16-surrogate.txt";
    constexpr std::string_view emojiContent = "A😀B";
    const auto emojiEncoded = nff::encoding::TextCodec::encode(
        emojiContent, nff::encoding::Encoding::Utf16LE, true);
    require(static_cast<bool>(emojiEncoded), "encode UTF-16 surrogate fixture");
    error = nff::storage::FileWriter::writeAtomically(emojiPath, emojiEncoded.bytes);
    require(!error, "write UTF-16 surrogate fixture");

    nff::viewer::LargeFileViewer emojiViewer;
    require(static_cast<bool>(emojiViewer.open(emojiPath, options)),
            "open UTF-16 surrogate viewer");
    const auto surrogateWindow = emojiViewer.readTextWindow(6U, 6U);
    require(static_cast<bool>(surrogateWindow), "read window beginning on low surrogate");
    require(surrogateWindow.byteStart == 4U,
            "UTF-16 window backs up to high surrogate boundary");
    require(surrogateWindow.text == "😀B",
            "UTF-16 surrogate window decodes complete code points");

    std::filesystem::remove_all(root, error);
}

void testAdaptiveInitialViewerWindowRecommendation() {
    const auto root = tempRoot("nff-viewer-initial-window");
    const auto normalPath = root / "normal.txt";
    const auto longPath = root / "long.txt";

    std::string normal;
    normal.reserve(2U * 1024U * 1024U);
    while (normal.size() < 2U * 1024U * 1024U) {
        normal += "0123456789abcdef0123456789abcdef\n";
    }
    auto error = nff::storage::FileWriter::writeAtomically(normalPath, asBytes(normal));
    require(!error, "write normal initial-window fixture");

    std::string longLine(2U * 1024U * 1024U, 'x');
    error = nff::storage::FileWriter::writeAtomically(longPath, asBytes(longLine));
    require(!error, "write long-line initial-window fixture");

    nff::viewer::ViewerOpenOptions options;
    options.performance = nff::viewer::PerformanceProfile::Fast;
    options.inspectOptions.viewerSizeThreshold = 1U;

    nff::viewer::LargeFileViewer normalViewer;
    require(static_cast<bool>(normalViewer.open(normalPath, options)),
            "open normal initial-window fixture");
    require(normalViewer.recommendedInitialTextWindowBytes() == 512U * 1024U,
            "fast viewer starts normal large files with a bounded 512 KiB foreground window");

    nff::viewer::LargeFileViewer longViewer;
    require(static_cast<bool>(longViewer.open(longPath, options)),
            "open long-line initial-window fixture");
    require(longViewer.recommendedInitialTextWindowBytes() == 128U * 1024U,
            "fast viewer shrinks the first foreground window for sampled giant lines");

    nff::viewer::ViewerOpenOptions saverOptions = options;
    saverOptions.performance = nff::viewer::PerformanceProfile::MemorySaver;
    nff::viewer::LargeFileViewer saverNormal;
    require(static_cast<bool>(saverNormal.open(normalPath, saverOptions)),
            "open memory-saver normal initial-window fixture");
    require(saverNormal.recommendedInitialTextWindowBytes() == 256U * 1024U,
            "memory saver primes only one 256 KiB cache block for normal text");

    nff::viewer::LargeFileViewer saverLong;
    require(static_cast<bool>(saverLong.open(longPath, saverOptions)),
            "open memory-saver long-line initial-window fixture");
    require(saverLong.recommendedInitialTextWindowBytes() == 128U * 1024U,
            "giant-line first window remains small in memory-saver mode");

    std::filesystem::remove_all(root, error);
}

void testFirstViewerWindowDefersReadAhead() {
    const auto root = tempRoot("nff-viewer-first-window-read-ahead");
    const auto path = root / "first-window.txt";
    constexpr std::size_t fixtureBytes = 8U * 1024U * 1024U;
    std::string content(fixtureBytes, 'x');
    for (std::size_t offset = 79U; offset < content.size(); offset += 80U) {
        content[offset] = '\n';
    }
    auto error = nff::storage::FileWriter::writeAtomically(path, asBytes(content));
    require(!error, "write first-window read-ahead fixture");

    nff::viewer::LargeFileViewer viewer;
    nff::viewer::ViewerOpenOptions options;
    options.performance = nff::viewer::PerformanceProfile::Fast;
    const auto opened = viewer.open(path, options);
    require(static_cast<bool>(opened), "open first-window read-ahead fixture");

    constexpr std::size_t visibleBytes = 2U * 1024U * 1024U;
    const auto first = viewer.readTextWindow(0U, visibleBytes);
    require(static_cast<bool>(first), "read first foreground viewer window");
    const auto firstStats = viewer.cacheStatistics();
    require(firstStats.residentBytes == visibleBytes,
            "first foreground viewer window does not synchronously read ahead");
    require(firstStats.misses == 2U,
            "first foreground viewer window loads only its visible cache blocks");

    const auto second = viewer.readTextWindow(visibleBytes, visibleBytes);
    require(static_cast<bool>(second), "read subsequent viewer window");
    const auto secondStats = viewer.cacheStatistics();
    require(secondStats.misses > firstStats.misses + 2U,
            "subsequent viewer windows retain fast-profile read ahead");

    std::filesystem::remove_all(root, error);
}

void testLargeViewerWindowAndProfileSwitch() {
    const auto root = tempRoot("nff-viewer-window");
    const auto path = root / "server.log";
    std::string content;
    for (std::size_t line = 1U; line <= 20000U; ++line) {
        content += "line-" + std::to_string(line) + " payload\n";
    }
    auto error = nff::storage::FileWriter::writeAtomically(path, asBytes(content));
    require(!error, "write viewer window fixture");

    nff::viewer::LargeFileViewer viewer;
    nff::viewer::ViewerOpenOptions options;
    options.performance = nff::viewer::PerformanceProfile::Fast;
    options.inspectOptions.viewerSizeThreshold = 1U;
    const auto opened = viewer.open(path, options);
    require(static_cast<bool>(opened), "open large viewer");
    require(viewer.profile().recommendedMode == nff::core::OpenMode::Viewer,
            "inspector recommends view mode");
    require(viewer.byteSearchCompatible(), "UTF-8 viewer supports streaming byte search");

    const auto window = viewer.readLines(10000U, 3U);
    require(static_cast<bool>(window), "read arbitrary large-file lines");
    require(window.text.find("line-10000 payload") == 0U, "window begins requested line");
    require(window.text.find("line-10002 payload") != std::string::npos,
            "window contains requested line count");

    const auto byteWindow = viewer.readTextWindow(content.size() / 2U, 4096U);
    require(static_cast<bool>(byteWindow), "read scalable byte-position text window");
    require(!byteWindow.text.empty(), "byte-position window decodes text");
    require(byteWindow.byteStart >= content.size() / 2U,
            "byte-position window starts at or after requested UTF-8 boundary");
    require(byteWindow.byteEnd <= content.size(), "byte-position window remains in file bounds");

    viewer.setPerformanceProfile(nff::viewer::PerformanceProfile::MemorySaver);
    require(viewer.performanceProfile() == nff::viewer::PerformanceProfile::MemorySaver,
            "runtime profile switch");
    require(viewer.cacheStatistics().residentBytes <= 16U * 1024U * 1024U,
            "memory saver budget after switch");

    std::filesystem::remove_all(root, error);
}

void testTailSearchAndPresentation() {
    const auto root = tempRoot("nff-viewer-tail");
    const auto path = root / "tail.log";
    const std::string content = "INFO boot\r\nWARN warm\nERROR failed\rINFO done\n";
    auto error = nff::storage::FileWriter::writeAtomically(path, asBytes(content));
    require(!error, "write tail fixture");

    nff::viewer::LargeFileViewer viewer;
    nff::viewer::ViewerOpenOptions options;
    options.performance = nff::viewer::PerformanceProfile::MemorySaver;
    const auto opened = viewer.open(path, options);
    require(static_cast<bool>(opened), "open tail fixture");

    const auto tail = viewer.readTail(2U);
    require(static_cast<bool>(tail), "read tail without full line index");
    require(tail.text == "ERROR failed\rINFO done\n", "tail returns last two logical lines");

    nff::search::SearchOptions searchOptions;
    searchOptions.caseSensitive = true;
    nff::search::StreamingSearchOptions streamingOptions;
    streamingOptions.chunkBytes = 7U;
    const auto matches = viewer.searchAll("ERROR", searchOptions, streamingOptions);
    require(static_cast<bool>(matches), "viewer streaming search");
    require(matches.matches.size() == 1U, "viewer search match count");
    require(matches.matches.front().offset == content.find("ERROR"), "viewer search byte offset");

    const auto nextWarn = viewer.search(
        "WARN", 0U, nff::search::SearchDirection::Forward, searchOptions, streamingOptions);
    require(static_cast<bool>(nextWarn) && nextWarn.match.has_value(),
            "viewer streaming find-next");
    require(nextWarn.match.has_value() && nextWarn.match->offset == content.find("WARN"),
            "viewer find-next byte offset");

    const auto previousInfo = viewer.search(
        "INFO", static_cast<std::uint64_t>(content.size()),
        nff::search::SearchDirection::Backward, searchOptions, streamingOptions);
    require(static_cast<bool>(previousInfo) && previousInfo.match.has_value(),
            "viewer streaming find-previous");
    require(previousInfo.match.has_value() &&
                previousInfo.match->offset == content.rfind("INFO"),
            "viewer find-previous returns last matching byte offset");

    const auto wrappedError = viewer.search(
        "ERROR", static_cast<std::uint64_t>(content.size()),
        nff::search::SearchDirection::Forward, searchOptions, streamingOptions);
    require(static_cast<bool>(wrappedError) && wrappedError.match.has_value() &&
                wrappedError.wrapped,
            "viewer streaming find reports wrap-around");

    nff::viewer::TextPresentationModel presentation;
    nff::viewer::LineFilter filter;
    filter.pattern = "ERROR";
    filter.mode = nff::viewer::LineFilterMode::IncludeMatches;
    presentation.setFilter(filter);

    nff::viewer::HighlightRule highlight;
    highlight.id = 42U;
    highlight.pattern = "failed";
    require(presentation.addHighlightRule(highlight), "add highlight rule");

    const auto visible = presentation.evaluate("ERROR failed");
    require(static_cast<bool>(visible) && visible.visible, "matching line visible");
    require(visible.highlights.size() == 1U, "highlight span emitted");
    require(visible.highlights.front().ruleId == 42U, "highlight rule id preserved");

    const auto hidden = presentation.evaluate("INFO ready");
    require(static_cast<bool>(hidden) && !hidden.visible, "nonmatching line filtered without mutation");

    std::filesystem::remove_all(root, error);
}

void testLiveFollowGrowthTruncateAndReplace() {
    const auto root = tempRoot("nff-viewer-follow");
    const auto path = root / "live.log";
    auto error = nff::storage::FileWriter::writeAtomically(path, asBytes("one\n"));
    require(!error, "write follow fixture");

    nff::viewer::LargeFileViewer viewer;
    const auto opened = viewer.open(path);
    require(static_cast<bool>(opened), "open follow fixture");

    nff::viewer::FollowOptions followOptions;
    followOptions.tailLines = 2U;
    nff::viewer::LiveFileFollower follower(viewer, followOptions);
    const auto initial = follower.snapshot();
    require(static_cast<bool>(initial) && initial.text == "one\n", "initial follow snapshot");

    {
        std::ofstream output(path, std::ios::binary | std::ios::app);
        output << "two\n";
        require(static_cast<bool>(output), "append follow fixture");
    }

    const auto grown = follower.poll();
    require(static_cast<bool>(grown), "poll appended file");
    require(grown.refresh.kind == nff::viewer::ViewerRefreshKind::Grown,
            "append classified as growth");
    require(grown.hasTail && grown.tail.text == "one\ntwo\n", "follow refreshes tail after growth");

    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "reset\n";
        require(static_cast<bool>(output), "truncate follow fixture");
    }

    const auto truncated = follower.poll();
    require(static_cast<bool>(truncated), "poll truncated file");
    require(truncated.refresh.kind == nff::viewer::ViewerRefreshKind::Truncated,
            "truncate classified correctly");
    require(truncated.hasTail && truncated.tail.text == "reset\n",
            "tail rebuilt after truncate");

    error = nff::storage::FileWriter::writeAtomically(path, asBytes("replacement\n"));
    require(!error, "atomically replace follow fixture");
    const auto replaced = follower.poll();
    require(static_cast<bool>(replaced), "poll replaced file");
    require(replaced.refresh.kind == nff::viewer::ViewerRefreshKind::Replaced,
            "atomic replacement detected by file identity");
    require(replaced.hasTail && replaced.tail.text == "replacement\n",
            "tail follows replacement path");

    std::filesystem::remove(path, error);
    require(!error, "delete followed file");
    const auto deleted = follower.poll();
    require(static_cast<bool>(deleted), "poll deleted file");
    require(deleted.refresh.kind == nff::viewer::ViewerRefreshKind::Deleted && !deleted.hasTail,
            "deleted path reported without stale tail");

    error = nff::storage::FileWriter::writeAtomically(path, asBytes("reborn\n"));
    require(!error, "recreate followed file");
    const auto recreated = follower.poll();
    require(static_cast<bool>(recreated), "poll recreated file");
    require(recreated.refresh.kind == nff::viewer::ViewerRefreshKind::Replaced,
            "recreated path classified as replacement");
    require(recreated.hasTail && recreated.tail.text == "reborn\n",
            "follow resumes after recreation");

    followOptions.maximumDecodedBytes = 8U;
    follower.setOptions(followOptions);
    {
        std::ofstream output(path, std::ios::binary | std::ios::app);
        output << "this-line-is-longer-than-eight-bytes\n";
        require(static_cast<bool>(output), "append oversized follow fixture");
    }
    const auto failedTail = follower.poll();
    require(!static_cast<bool>(failedTail) && failedTail.error == std::errc::value_too_large,
            "follow reports transient tail snapshot failure");
    require(failedTail.refresh.kind == nff::viewer::ViewerRefreshKind::Grown,
            "failed tail snapshot still advances viewer refresh baseline");

    followOptions.maximumDecodedBytes = 1024U;
    follower.setOptions(followOptions);
    const auto retriedTail = follower.poll();
    require(static_cast<bool>(retriedTail), "follow retries pending tail snapshot");
    require(retriedTail.refresh.kind == nff::viewer::ViewerRefreshKind::Unchanged,
            "pending tail retry does not require another file change");
    require(retriedTail.hasTail &&
                retriedTail.tail.text.find("this-line-is-longer-than-eight-bytes") != std::string::npos,
            "pending tail retry recovers after transient failure");

    std::filesystem::remove_all(root, error);
}

void testUtf16Tail() {
    const auto root = tempRoot("nff-viewer-tail-utf16");
    const auto path = root / "utf16.log";
    const auto encoded = nff::encoding::TextCodec::encode(
        "alpha\r\nbeta\r\ngamma\r\n", nff::encoding::Encoding::Utf16LE, true);
    require(static_cast<bool>(encoded), "encode UTF-16 tail fixture");
    auto error = nff::storage::FileWriter::writeAtomically(path, encoded.bytes);
    require(!error, "write UTF-16 tail fixture");

    nff::viewer::LargeFileViewer viewer;
    const auto opened = viewer.open(path);
    require(static_cast<bool>(opened), "open UTF-16 tail fixture");
    const auto tail = viewer.readTail(2U);
    require(static_cast<bool>(tail), "read UTF-16 tail");
    require(tail.text == "beta\r\ngamma\r\n", "UTF-16 tail handles CRLF boundaries");

    const auto limited = viewer.readTail(3U, 8U);
    require(!static_cast<bool>(limited) && limited.error == std::errc::value_too_large,
            "tail enforces decoded memory ceiling");

    std::filesystem::remove_all(root, error);
}

void testFixedWidthUnicodeStreamingSearch() {
    const auto root = tempRoot("nff-viewer-fixed-width-search");
    constexpr std::string_view content = "SCAT alpha\nCAT beta\ncat gamma\nOMEGA CAT end\n";

    for (const auto encoding : {nff::encoding::Encoding::Utf16LE,
                                nff::encoding::Encoding::Utf16BE,
                                nff::encoding::Encoding::Utf32LE,
                                nff::encoding::Encoding::Utf32BE}) {
        const auto encoded = nff::encoding::TextCodec::encode(content, encoding, true);
        require(static_cast<bool>(encoded), "encode fixed-width search fixture");
        const auto path = root / (std::string("fixed-") +
                                  std::to_string(static_cast<int>(encoding)) + ".txt");
        auto error = nff::storage::FileWriter::writeAtomically(path, encoded.bytes);
        require(!error, "write fixed-width search fixture");

        nff::viewer::ViewerOpenOptions openOptions;
        openOptions.performance = nff::viewer::PerformanceProfile::MemorySaver;
        openOptions.inspectOptions.viewerSizeThreshold = 1U;
        nff::viewer::LargeFileViewer viewer;
        require(static_cast<bool>(viewer.open(path, openOptions)),
                "open fixed-width search viewer");

        nff::search::SearchOptions options;
        options.caseSensitive = false;
        options.wholeWord = true;
        nff::search::StreamingSearchOptions streaming;
        streaming.chunkBytes = 9U;

        const auto first = viewer.search(
            "cat", 0U, nff::search::SearchDirection::Forward, options, streaming);
        require(static_cast<bool>(first) && first.match.has_value(),
                "fixed-width viewer finds whole-word literal");

        const auto expectedPrefix = nff::encoding::TextCodec::encode(
            "SCAT alpha\n", encoding, true);
        const auto expectedPattern = nff::encoding::TextCodec::encode(
            "CAT", encoding, false);
        require(static_cast<bool>(expectedPrefix) && static_cast<bool>(expectedPattern),
                "encode fixed-width expected offsets");
        require(first.match.has_value() &&
                    first.match->offset == expectedPrefix.bytes.size() &&
                    first.match->length == expectedPattern.bytes.size(),
                "fixed-width search returns encoded-file byte offsets");

        const auto all = viewer.searchAll("cat", options, streaming);
        require(static_cast<bool>(all), "fixed-width viewer search-all succeeds");
        require(all.matches.size() == 3U,
                "fixed-width whole-word search excludes SCAT and finds three standalone matches");

        const auto previous = viewer.search(
            "cat", static_cast<std::uint64_t>(encoded.bytes.size()),
            nff::search::SearchDirection::Backward, options, streaming);
        require(static_cast<bool>(previous) && previous.match.has_value() &&
                    previous.match->offset == all.matches.back().offset,
                "fixed-width backward search returns final standalone match");

        const auto wrapped = viewer.search(
            "cat", static_cast<std::uint64_t>(encoded.bytes.size()),
            nff::search::SearchDirection::Forward, options, streaming);
        require(static_cast<bool>(wrapped) && wrapped.match.has_value() && wrapped.wrapped &&
                    wrapped.match->offset == all.matches.front().offset,
                "fixed-width forward search preserves wrap semantics");

        auto regex = options;
        regex.kind = nff::search::SearchKind::RegularExpression;
        const auto regexResult = viewer.search(
            "c.t", 0U, nff::search::SearchDirection::Forward, regex, streaming);
        require(!static_cast<bool>(regexResult) &&
                    regexResult.error == std::errc::operation_not_supported,
                "scalable fixed-width regex remains an explicit bounded limitation");
    }

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

}

int main() {
    testPreparedViewerOpenApiExists();
    testPreparedViewerOpenReusesStableInspection();
    testPreparedViewerOpenFallsBackWhenFileChanged();
    testRandomAccessAndCache();
    testLineIndexMixedEndings();
    testUtf16LineIndexAndViewer();
    testAdaptiveInitialViewerWindowRecommendation();
    testFirstViewerWindowDefersReadAhead();
    testLargeViewerWindowAndProfileSwitch();
    testTailSearchAndPresentation();
    testLiveFollowGrowthTruncateAndReplace();
    testUtf16Tail();
    testFixedWidthUnicodeStreamingSearch();
    return EXIT_SUCCESS;
}
