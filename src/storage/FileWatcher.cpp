#include "notepadFasaFiso/storage/FileWatcher.hpp"

#include <limits>

namespace nff::storage {

WatchResult FileWatcher::watch(const std::filesystem::path& path) {
    if (path.empty()) {
        return {{}, std::make_error_code(std::errc::invalid_argument)};
    }

    const auto captured = FileStateTracker::capture(path);
    if (!captured) {
        return {{}, captured.error};
    }

    const auto id = allocateId();
    if (!id) {
        return {{}, std::make_error_code(std::errc::value_too_large)};
    }

    WatchedFile file;
    file.path = path;
    file.state = captured.state;
    file.identity = captured.state.identity;
    watched_.emplace(id, std::move(file));
    return {id, {}};
}

bool FileWatcher::unwatch(const WatchId id) noexcept {
    return watched_.erase(id) != 0U;
}

void FileWatcher::clear() noexcept {
    watched_.clear();
}

std::size_t FileWatcher::size() const noexcept {
    return watched_.size();
}

std::vector<FileWatchEvent> FileWatcher::poll() {
    std::vector<FileWatchEvent> events;
    events.reserve(watched_.size());

    for (auto& [id, watched] : watched_) {
        const auto current = FileStateTracker::capture(watched.path);
        if (!current) {
            if (!watched.inaccessible) {
                events.push_back({id, watched.path, FileWatchEventKind::Inaccessible,
                                  watched.state, watched.identity, current.error});
            }
            watched.inaccessible = true;
            continue;
        }

        const bool accessRestored = watched.inaccessible;
        watched.inaccessible = false;

        const auto currentIdentity = current.state.identity;

        FileWatchEventKind kind{FileWatchEventKind::Modified};
        bool changed = false;

        if (!watched.state.exists && current.state.exists) {
            kind = FileWatchEventKind::Created;
            changed = true;
        } else if (watched.state.exists && !current.state.exists) {
            kind = FileWatchEventKind::Deleted;
            changed = true;
        } else if (watched.state.exists && current.state.exists) {
            if (watched.identity.valid && currentIdentity.valid &&
                watched.identity != currentIdentity) {
                kind = FileWatchEventKind::Replaced;
                changed = true;
            } else if (watched.state.size != current.state.size ||
                       watched.state.writeTime != current.state.writeTime) {
                kind = FileWatchEventKind::Modified;
                changed = true;
            }
        }

        if (changed) {
            events.push_back({id, watched.path, kind, current.state, currentIdentity, {}});
        } else if (accessRestored) {
            events.push_back({id, watched.path, FileWatchEventKind::AccessRestored,
                              current.state, currentIdentity, {}});
        }

        watched.state = current.state;
        watched.identity = currentIdentity;
    }

    return events;
}

WatchId FileWatcher::allocateId() noexcept {
    if (nextId_ == 0U || nextId_ == std::numeric_limits<std::uint64_t>::max()) {
        return {};
    }
    return WatchId{nextId_++};
}

}
