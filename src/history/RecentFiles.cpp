#include "notepadFasaFiso/history/RecentFiles.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

namespace nff::history {

RecentFiles::RecentFiles(const std::size_t capacity) noexcept {
    setCapacity(capacity);
}

void RecentFiles::setCapacity(const std::size_t capacity) noexcept {
    capacity_ = std::clamp(capacity, std::size_t{1U}, maximumCapacity);
    trimToCapacity();
}

std::size_t RecentFiles::capacity() const noexcept {
    return capacity_;
}

std::span<const RecentFileEntry> RecentFiles::entries() const noexcept {
    return entries_;
}

bool RecentFiles::empty() const noexcept {
    return entries_.empty();
}

std::size_t RecentFiles::size() const noexcept {
    return entries_.size();
}

void RecentFiles::touch(const std::filesystem::path& path,
                        const core::OpenMode mode,
                        std::uint64_t unixMilliseconds) {
    if (path.empty()) {
        return;
    }
    if (unixMilliseconds == 0U) {
        unixMilliseconds = nowUnixMilliseconds();
    }

    auto iterator = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) {
        return samePath(entry.path, path);
    });

    RecentFileEntry updated;
    if (iterator != entries_.end()) {
        updated = std::move(*iterator);
        entries_.erase(iterator);
    } else {
        updated.path = path;
    }

    updated.path = path;
    updated.lastOpenedUnixMilliseconds = unixMilliseconds;
    updated.lastOpenMode = mode;
    if (updated.openCount != std::numeric_limits<std::uint64_t>::max()) {
        ++updated.openCount;
    }
    entries_.insert(entries_.begin(), std::move(updated));
    trimToCapacity();
}

bool RecentFiles::remove(const std::filesystem::path& path) {
    const auto originalSize = entries_.size();
    std::erase_if(entries_, [&](const auto& entry) { return samePath(entry.path, path); });
    return entries_.size() != originalSize;
}

std::size_t RecentFiles::pruneMissing() {
    const auto originalSize = entries_.size();
    std::erase_if(entries_, [](const auto& entry) {
        std::error_code error;
        const bool exists = std::filesystem::is_regular_file(entry.path, error);
        return !error && !exists;
    });
    return originalSize - entries_.size();
}

void RecentFiles::clear() noexcept {
    entries_.clear();
}

bool RecentFiles::replace(std::vector<RecentFileEntry> entries) {
    if (entries.size() > maximumCapacity) {
        return false;
    }

    for (const auto& entry : entries) {
        if (entry.path.empty() || entry.openCount == 0U ||
            entry.lastOpenedUnixMilliseconds == 0U ||
            static_cast<std::uint8_t>(entry.lastOpenMode) >
                static_cast<std::uint8_t>(core::OpenMode::BinaryPreview)) {
            return false;
        }
    }

    std::vector<RecentFileEntry> deduplicated;
    deduplicated.reserve(entries.size());
    for (auto& entry : entries) {
        const auto duplicate = std::find_if(deduplicated.begin(), deduplicated.end(),
                                            [&](const auto& existing) {
                                                return samePath(existing.path, entry.path);
                                            });
        if (duplicate != deduplicated.end()) {
            return false;
        }
        deduplicated.push_back(std::move(entry));
    }

    std::ranges::sort(deduplicated, [](const auto& left, const auto& right) {
        return left.lastOpenedUnixMilliseconds > right.lastOpenedUnixMilliseconds;
    });
    entries_ = std::move(deduplicated);
    trimToCapacity();
    return true;
}

bool RecentFiles::samePath(const std::filesystem::path& left,
                           const std::filesystem::path& right) {
    std::error_code error;
    if (std::filesystem::equivalent(left, right, error) && !error) {
        return true;
    }

    error.clear();
    auto normalizedLeft = std::filesystem::weakly_canonical(left, error);
    if (error) {
        error.clear();
        normalizedLeft = std::filesystem::absolute(left, error);
        if (error) {
            normalizedLeft = left;
        }
        normalizedLeft = normalizedLeft.lexically_normal();
    }

    error.clear();
    auto normalizedRight = std::filesystem::weakly_canonical(right, error);
    if (error) {
        error.clear();
        normalizedRight = std::filesystem::absolute(right, error);
        if (error) {
            normalizedRight = right;
        }
        normalizedRight = normalizedRight.lexically_normal();
    }
    return normalizedLeft == normalizedRight;
}

std::uint64_t RecentFiles::nowUnixMilliseconds() noexcept {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return milliseconds <= 0 ? 1U : static_cast<std::uint64_t>(milliseconds);
}

void RecentFiles::trimToCapacity() noexcept {
    if (entries_.size() > capacity_) {
        entries_.resize(capacity_);
    }
}

}
