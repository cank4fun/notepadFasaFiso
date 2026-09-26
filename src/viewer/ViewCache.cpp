#include "notepadFasaFiso/viewer/ViewCache.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace nff::viewer {
namespace {

constexpr std::size_t kKiB = 1024U;
constexpr std::size_t kMiB = 1024U * 1024U;
constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;

[[nodiscard]] CachePolicy normalized(CachePolicy policy) noexcept {
    policy.blockBytes = std::max<std::size_t>(4U * kKiB, policy.blockBytes);
    policy.maximumBytes = std::max(policy.blockBytes, policy.maximumBytes);
    return policy;
}

}

ViewCache::ViewCache(CachePolicy policy) : policy_(normalized(policy)) {
    const auto blockCount = policy_.maximumBytes / policy_.blockBytes;
    blocks_.reserve(std::max<std::size_t>(1U, blockCount));
}

void ViewCache::setPolicy(CachePolicy policy) {
    policy_ = normalized(policy);
    clear();
    const auto blockCount = policy_.maximumBytes / policy_.blockBytes;
    blocks_.reserve(std::max<std::size_t>(1U, blockCount));
}

const CachePolicy& ViewCache::policy() const noexcept { return policy_; }
CacheStatistics ViewCache::statistics() const noexcept { return statistics_; }

void ViewCache::clear() noexcept {
    blocks_.clear();
    lru_.clear();
    statistics_.residentBytes = 0U;
    statistics_.residentBlocks = 0U;
}

void ViewCache::touch(Block& block) noexcept {
    lru_.splice(lru_.begin(), lru_, block.lru);
}

void ViewCache::trim() noexcept {
    while (statistics_.residentBytes > policy_.maximumBytes && !lru_.empty()) {
        const auto victim = lru_.back();
        lru_.pop_back();
        const auto found = blocks_.find(victim);
        if (found == blocks_.end()) {
            continue;
        }
        statistics_.residentBytes -= found->second.bytes.size();
        blocks_.erase(found);
        ++statistics_.evictions;
    }
    statistics_.residentBlocks = blocks_.size();
}

ViewCache::Block* ViewCache::getBlock(storage::RandomAccessFile& file,
                                      const std::uint64_t blockIndex,
                                      std::error_code& error) {
    if (const auto found = blocks_.find(blockIndex); found != blocks_.end()) {
        ++statistics_.hits;
        touch(found->second);
        return &found->second;
    }

    ++statistics_.misses;
    if (policy_.blockBytes == 0U ||
        blockIndex > std::numeric_limits<std::uint64_t>::max() /
                         static_cast<std::uint64_t>(policy_.blockBytes)) {
        error = std::make_error_code(std::errc::value_too_large);
        return nullptr;
    }

    const auto blockOffset = blockIndex * static_cast<std::uint64_t>(policy_.blockBytes);
    if (blockOffset >= file.size()) {
        error.clear();
        return nullptr;
    }

    const auto remaining = file.size() - blockOffset;
    const auto bytesToRead = static_cast<std::size_t>(std::min<std::uint64_t>(
        remaining, static_cast<std::uint64_t>(policy_.blockBytes)));

    Block block;
    block.bytes.resize(bytesToRead);
    const auto read = file.readAt(blockOffset, block.bytes);
    if (!read) {
        error = read.error;
        return nullptr;
    }
    if (read.bytesRead != bytesToRead) {
        error = std::make_error_code(std::errc::io_error);
        return nullptr;
    }

    auto [inserted, wasInserted] = blocks_.emplace(blockIndex, std::move(block));
    if (!wasInserted) {
        error = std::make_error_code(std::errc::state_not_recoverable);
        return nullptr;
    }
    try {
        lru_.push_front(blockIndex);
    } catch (...) {
        blocks_.erase(inserted);
        throw;
    }
    inserted->second.lru = lru_.begin();
    statistics_.residentBytes += inserted->second.bytes.size();
    trim();

    if (statistics_.residentBytes > policy_.maximumBytes) {
        error = std::make_error_code(std::errc::not_enough_memory);
        return nullptr;
    }

    error.clear();
    return &inserted->second;
}

CacheReadResult ViewCache::read(storage::RandomAccessFile& file,
                                const std::uint64_t offset,
                                const std::span<std::byte> destination) {
    if (!file.isOpen()) {
        return {0U, std::make_error_code(std::errc::bad_file_descriptor)};
    }
    if (offset > file.size()) {
        return {0U, std::make_error_code(std::errc::invalid_argument)};
    }
    if (destination.empty() || offset == file.size()) {
        return {};
    }

    const auto available = file.size() - offset;
    const auto target = static_cast<std::size_t>(std::min<std::uint64_t>(
        available, static_cast<std::uint64_t>(destination.size())));
    std::size_t completed = 0U;

    while (completed < target) {
        const auto absolute = offset + static_cast<std::uint64_t>(completed);
        const auto blockIndex = absolute / static_cast<std::uint64_t>(policy_.blockBytes);
        const auto within = static_cast<std::size_t>(
            absolute % static_cast<std::uint64_t>(policy_.blockBytes));

        std::error_code error;
        auto* block = getBlock(file, blockIndex, error);
        if (error) {
            return {completed, error};
        }
        if (block == nullptr || within >= block->bytes.size()) {
            break;
        }

        const auto copyBytes = std::min(target - completed, block->bytes.size() - within);
        std::memcpy(destination.data() + completed, block->bytes.data() + within, copyBytes);
        completed += copyBytes;
    }

    return {completed, {}};
}

std::error_code ViewCache::prefetch(storage::RandomAccessFile& file,
                                    const std::uint64_t offset,
                                    const std::size_t blockCount) {
    if (!file.isOpen()) {
        return std::make_error_code(std::errc::bad_file_descriptor);
    }
    if (offset >= file.size() || blockCount == 0U) {
        return {};
    }

    auto blockIndex = offset / static_cast<std::uint64_t>(policy_.blockBytes);
    for (std::size_t index = 0U; index < blockCount; ++index, ++blockIndex) {
        std::error_code error;
        static_cast<void>(getBlock(file, blockIndex, error));
        if (error) {
            return error;
        }
        const auto nextOffset = (blockIndex + 1U) * static_cast<std::uint64_t>(policy_.blockBytes);
        if (nextOffset >= file.size()) {
            break;
        }
    }
    return {};
}

PerformanceProfile ViewCache::resolveProfile(const PerformanceProfile requested,
                                             const std::uint64_t fileSize,
                                             const std::uint64_t availableMemoryBytes) noexcept {
    if (requested != PerformanceProfile::Automatic) {
        return requested;
    }

    if (availableMemoryBytes >= 4U * kGiB && fileSize >= 64U * kMiB) {
        return PerformanceProfile::Fast;
    }
    return PerformanceProfile::MemorySaver;
}

CachePolicy ViewCache::makePolicy(const PerformanceProfile profile,
                                  const std::uint64_t fileSize,
                                  const std::uint64_t availableMemoryBytes) noexcept {
    const auto resolved = resolveProfile(profile, fileSize, availableMemoryBytes);
    if (resolved == PerformanceProfile::MemorySaver) {
        return {256U * kKiB, 16U * kMiB, 1U};
    }

    std::uint64_t target = 128U * kMiB;
    if (availableMemoryBytes != 0U) {
        target = std::clamp<std::uint64_t>(availableMemoryBytes / 32U,
                                           64U * kMiB,
                                           512U * kMiB);
    }
    target = std::min(target, std::max<std::uint64_t>(fileSize, 1U * kMiB));
    const auto maximum = static_cast<std::size_t>(std::min<std::uint64_t>(
        target, static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())));
    return {1U * kMiB, std::max<std::size_t>(1U * kMiB, maximum), 4U};
}

}
