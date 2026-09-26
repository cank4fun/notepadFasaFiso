#pragma once

#include "notepadFasaFiso/storage/FileState.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <system_error>
#include <vector>

namespace nff::storage {

struct WatchId final {
    std::uint64_t value{0};

    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
    friend bool operator==(WatchId, WatchId) = default;
    friend auto operator<=>(WatchId, WatchId) = default;
};

enum class FileWatchEventKind : std::uint8_t {
    Modified,
    Deleted,
    Created,
    Replaced,
    AccessRestored,
    Inaccessible,
};

struct FileWatchEvent final {
    WatchId id{};
    std::filesystem::path path;
    FileWatchEventKind kind{FileWatchEventKind::Modified};
    FileState state{};
    FileIdentity identity{};
    std::error_code error{};
};

struct WatchResult final {
    WatchId id{};
    std::error_code error{};

    [[nodiscard]] explicit operator bool() const noexcept { return !error && static_cast<bool>(id); }
};

class FileWatcher final {
public:
    [[nodiscard]] WatchResult watch(const std::filesystem::path& path);
    [[nodiscard]] bool unwatch(WatchId id) noexcept;
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::vector<FileWatchEvent> poll();

private:
    struct WatchedFile final {
        std::filesystem::path path;
        FileState state{};
        FileIdentity identity{};
        bool inaccessible{false};
    };

    [[nodiscard]] WatchId allocateId() noexcept;

    std::map<WatchId, WatchedFile> watched_;
    std::uint64_t nextId_{1};
};

}
