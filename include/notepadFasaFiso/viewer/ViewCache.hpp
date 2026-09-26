#pragma once

#include "notepadFasaFiso/storage/RandomAccessFile.hpp"

#include <cstddef>
#include <cstdint>
#include <list>
#include <span>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace nff::viewer {

enum class PerformanceProfile : std::uint8_t {
    Automatic,
    Fast,
    MemorySaver,
};

struct CachePolicy final {
    std::size_t blockBytes{256U * 1024U};
    std::size_t maximumBytes{16U * 1024U * 1024U};
    std::size_t prefetchBlocks{1U};
};

struct CacheStatistics final {
    std::uint64_t hits{};
    std::uint64_t misses{};
    std::uint64_t evictions{};
    std::size_t residentBytes{};
    std::size_t residentBlocks{};
};

struct CacheReadResult final {
    std::size_t bytesRead{};
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

class ViewCache final {
public:
    explicit ViewCache(CachePolicy policy = {});

    void setPolicy(CachePolicy policy);
    [[nodiscard]] const CachePolicy& policy() const noexcept;
    [[nodiscard]] CacheStatistics statistics() const noexcept;
    void clear() noexcept;

    [[nodiscard]] CacheReadResult read(storage::RandomAccessFile& file,
                                       std::uint64_t offset,
                                       std::span<std::byte> destination);
    [[nodiscard]] std::error_code prefetch(storage::RandomAccessFile& file,
                                           std::uint64_t offset,
                                           std::size_t blockCount);

    [[nodiscard]] static CachePolicy makePolicy(PerformanceProfile profile,
                                                std::uint64_t fileSize,
                                                std::uint64_t availableMemoryBytes = 0U) noexcept;
    [[nodiscard]] static PerformanceProfile resolveProfile(PerformanceProfile requested,
                                                           std::uint64_t fileSize,
                                                           std::uint64_t availableMemoryBytes) noexcept;

private:
    struct Block final {
        std::vector<std::byte> bytes;
        std::list<std::uint64_t>::iterator lru;
    };

    [[nodiscard]] Block* getBlock(storage::RandomAccessFile& file,
                                  std::uint64_t blockIndex,
                                  std::error_code& error);
    void touch(Block& block) noexcept;
    void trim() noexcept;

    CachePolicy policy_;
    std::unordered_map<std::uint64_t, Block> blocks_;
    std::list<std::uint64_t> lru_;
    CacheStatistics statistics_;
};

}
