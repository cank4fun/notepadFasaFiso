#pragma once

#include "notepadFasaFiso/core/FileSniffer.hpp"
#include "notepadFasaFiso/encoding/EncodingDetector.hpp"
#include "notepadFasaFiso/search/SearchSource.hpp"
#include "notepadFasaFiso/storage/FileState.hpp"
#include "notepadFasaFiso/storage/RandomAccessFile.hpp"
#include "notepadFasaFiso/viewer/LineIndex.hpp"
#include "notepadFasaFiso/viewer/ViewCache.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace nff::viewer {

struct ViewerOpenOptions final {
    PerformanceProfile performance{PerformanceProfile::Automatic};
    std::optional<encoding::Encoding> overrideEncoding;
    core::InspectOptions inspectOptions{};
};

struct ViewerOpenResult final {
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

struct RawWindowResult final {
    std::vector<std::byte> bytes;
    std::uint64_t offset{};
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

struct TextWindowResult final {
    std::string text;
    std::uint64_t firstLine{1U};
    std::uint64_t byteStart{};
    std::uint64_t byteEnd{};
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

struct TailWindowResult final {
    std::string text;
    std::uint64_t byteStart{};
    std::uint64_t byteEnd{};
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

enum class ViewerRefreshKind : std::uint8_t {
    Unchanged,
    Grown,
    Truncated,
    Modified,
    Replaced,
    Deleted,
    Inaccessible,
};

struct ViewerRefreshResult final {
    ViewerRefreshKind kind{ViewerRefreshKind::Unchanged};
    std::uint64_t previousSize{};
    std::uint64_t currentSize{};
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
    [[nodiscard]] bool changed() const noexcept {
        return kind != ViewerRefreshKind::Unchanged;
    }
};

class LargeFileViewer final : public search::ITextSearchSource {
public:
    LargeFileViewer() = default;

    [[nodiscard]] ViewerOpenResult open(const std::filesystem::path& path,
                                        const ViewerOpenOptions& options = {});
    [[nodiscard]] ViewerOpenResult openPrepared(
        const std::filesystem::path& path,
        const core::DocumentProfile& profile,
        const storage::FileState& inspectedState,
        const ViewerOpenOptions& options = {});
    void close() noexcept;

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] std::uint64_t size() const noexcept override;
    [[nodiscard]] search::SourceReadResult read(std::uint64_t offset,
                                                std::span<char> destination) override;

    [[nodiscard]] RawWindowResult readRaw(std::uint64_t offset, std::size_t maximumBytes);
    [[nodiscard]] TextWindowResult readTextWindow(
        std::uint64_t byteOffset,
        std::size_t maximumEncodedBytes = 4U * 1024U * 1024U);
    [[nodiscard]] TextWindowResult readLines(std::uint64_t firstLine,
                                             std::uint64_t lineCount,
                                             std::size_t maximumDecodedBytes = 16U * 1024U * 1024U);
    [[nodiscard]] TailWindowResult readTail(std::uint64_t lineCount,
                                            std::size_t maximumDecodedBytes = 16U * 1024U * 1024U);

    [[nodiscard]] LineOffsetResult lineStart(std::uint64_t oneBasedLine);
    [[nodiscard]] OffsetLineResult lineAtOffset(std::uint64_t byteOffset);
    [[nodiscard]] LineCountResult lineCount();

    [[nodiscard]] ViewerRefreshResult refresh();
    [[nodiscard]] search::StreamingFindResult search(
        std::string_view pattern,
        std::uint64_t startOffset,
        search::SearchDirection direction,
        const search::SearchOptions& options = {},
        const search::StreamingSearchOptions& streamingOptions = {});
    [[nodiscard]] search::StreamingSearchResult searchAll(
        std::string_view pattern,
        const search::SearchOptions& options = {},
        const search::StreamingSearchOptions& streamingOptions = {});

    void setPerformanceProfile(PerformanceProfile profile);
    [[nodiscard]] PerformanceProfile performanceProfile() const noexcept;
    [[nodiscard]] CacheStatistics cacheStatistics() const noexcept;
    [[nodiscard]] LineIndexStatistics lineIndexStatistics() const noexcept;
    [[nodiscard]] std::size_t recommendedInitialTextWindowBytes() const noexcept;
    [[nodiscard]] const core::DocumentProfile& profile() const noexcept;
    [[nodiscard]] bool byteSearchCompatible() const noexcept;

private:
    [[nodiscard]] static std::uint64_t bomBytes(encoding::Encoding encoding, bool hasBom) noexcept;
    [[nodiscard]] RawWindowResult readRawWindow(std::uint64_t offset,
                                                std::size_t maximumBytes,
                                                bool allowReadAhead);
    [[nodiscard]] encoding::DetectionResult effectiveDetection() const noexcept;
    [[nodiscard]] bool fixedWidthWholeWordMatch(
        const search::SearchMatch& match,
        encoding::Encoding encoding,
        std::uint64_t contentStart,
        std::error_code& error);
    void configurePerformance(PerformanceProfile requested);

    storage::RandomAccessFile file_;
    ViewCache cache_;
    LineIndex lineIndex_;
    core::DocumentProfile profile_;
    ViewerOpenOptions openOptions_{};
    storage::FileState baselineState_{};
    PerformanceProfile requestedPerformance_{PerformanceProfile::Automatic};
    PerformanceProfile resolvedPerformance_{PerformanceProfile::MemorySaver};
    std::optional<encoding::Encoding> overrideEncoding_;
    bool textWindowPrimed_{false};
};

}
