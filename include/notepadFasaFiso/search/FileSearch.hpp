#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace nff::search {

enum class FileSearchEntryKind : std::uint8_t {
    File,
    Directory,
};

struct FileSearchHit final {
    std::filesystem::path path;
    std::string relativePathUtf8;
    std::uint32_t score{0};
    FileSearchEntryKind kind{FileSearchEntryKind::File};
};

struct FileSearchBuildOptions final {
    bool includeHidden{false};
    bool includeDirectories{true};
    bool followDirectorySymlinks{false};
    std::size_t maxDepth{64U};
    std::size_t maxEntries{500'000U};
    std::vector<std::string> excludedDirectoryNames{
        ".git", ".svn", ".hg", "node_modules", ".cache",
        "$Recycle.Bin", "System Volume Information"
    };
};

struct FileSearchBuildStats final {
    std::size_t indexedFiles{0};
    std::size_t indexedDirectories{0};
    std::size_t skippedEntries{0};
    std::size_t errors{0};
    bool truncated{false};
    bool cancelled{false};
};

struct FileSearchBuildResult final {
    FileSearchBuildStats stats{};
    std::error_code error{};

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

class FileSearchIndex final {
public:
    using CancelPredicate = std::function<bool()>;

    [[nodiscard]] FileSearchBuildResult rebuild(
        std::span<const std::filesystem::path> roots,
        const FileSearchBuildOptions& options = {},
        const CancelPredicate& shouldCancel = {});

    [[nodiscard]] std::vector<FileSearchHit> search(
        std::string_view query,
        std::size_t maxResults = 100U,
        const CancelPredicate& shouldCancel = {}) const;

    [[nodiscard]] bool upsert(const std::filesystem::path& path,
                              const std::filesystem::path& root = {});
    [[nodiscard]] bool erase(const std::filesystem::path& path);
    [[nodiscard]] std::size_t eraseSubtree(const std::filesystem::path& path);
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] std::size_t estimatedStorageBytes() const noexcept;

private:
    struct Entry final {
        std::uint32_t pathOffset{0U};
        std::uint32_t pathLength{0U};
        std::uint32_t filenameOffset{0U};
        std::uint32_t rootIndex{0U};
        FileSearchEntryKind kind{FileSearchEntryKind::File};
    };

    static_assert(sizeof(Entry) <= 20U, "File search entries must remain compact");

    mutable std::shared_mutex mutex_;
    std::vector<Entry> entries_;
    std::string pathArena_;
    std::vector<std::filesystem::path> roots_;
    std::uint64_t generation_{0U};
};

}
