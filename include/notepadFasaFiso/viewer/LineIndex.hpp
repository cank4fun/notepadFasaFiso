#pragma once

#include "notepadFasaFiso/encoding/EncodingDetector.hpp"
#include "notepadFasaFiso/storage/RandomAccessFile.hpp"
#include "notepadFasaFiso/viewer/ViewCache.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <system_error>
#include <vector>

namespace nff::viewer {

struct LineIndexOptions final {
    std::uint64_t checkpointStrideLines{4096U};
    std::size_t scanChunkBytes{1U * 1024U * 1024U};
};

struct LineOffsetResult final {
    std::optional<std::uint64_t> offset;
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

struct OffsetLineResult final {
    std::optional<std::uint64_t> line;
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

struct LineCountResult final {
    std::uint64_t lines{};
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

struct LineIndexStatistics final {
    std::size_t checkpoints{};
    std::uint64_t furthestIndexedOffset{};
    std::uint64_t furthestIndexedLine{1U};
};

class LineIndex final {
public:
    LineIndex() = default;

    void reset(encoding::Encoding encoding,
               std::uint64_t contentStart,
               std::uint64_t fileSize,
               LineIndexOptions options = {});

    [[nodiscard]] LineOffsetResult lineStart(storage::RandomAccessFile& file,
                                             ViewCache& cache,
                                             std::uint64_t oneBasedLine);
    [[nodiscard]] OffsetLineResult lineAtOffset(storage::RandomAccessFile& file,
                                                ViewCache& cache,
                                                std::uint64_t byteOffset);
    [[nodiscard]] LineCountResult lineCount(storage::RandomAccessFile& file,
                                            ViewCache& cache);

    [[nodiscard]] LineIndexStatistics statistics() const noexcept;
    [[nodiscard]] std::uint64_t contentStart() const noexcept;

    [[nodiscard]] static LineIndexOptions makeOptions(PerformanceProfile profile) noexcept;

private:
    struct Checkpoint final {
        std::uint64_t line{1U};
        std::uint64_t offset{};
    };

    struct ScanResult final {
        std::uint64_t line{1U};
        std::uint64_t lineStart{};
        std::uint64_t scannedUntil{};
        bool reachedEnd{false};
        std::error_code error;
    };

    [[nodiscard]] ScanResult scanToLine(storage::RandomAccessFile& file,
                                        ViewCache& cache,
                                        Checkpoint start,
                                        std::uint64_t targetLine);
    [[nodiscard]] ScanResult scanToOffset(storage::RandomAccessFile& file,
                                          ViewCache& cache,
                                          Checkpoint start,
                                          std::uint64_t targetOffset);
    [[nodiscard]] ScanResult scanToEnd(storage::RandomAccessFile& file,
                                       ViewCache& cache,
                                       Checkpoint start);

    [[nodiscard]] Checkpoint checkpointForLine(std::uint64_t line) const noexcept;
    [[nodiscard]] Checkpoint checkpointForOffset(std::uint64_t offset) const noexcept;
    void remember(std::uint64_t line, std::uint64_t offset);

    encoding::Encoding encoding_{encoding::Encoding::Utf8};
    std::uint64_t contentStart_{};
    std::uint64_t fileSize_{};
    LineIndexOptions options_{};
    std::vector<Checkpoint> checkpoints_;
    std::uint64_t furthestIndexedOffset_{};
    std::uint64_t furthestIndexedLine_{1U};
};

}
