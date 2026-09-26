#pragma once

#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/storage/FileWatcher.hpp"

#include <cstddef>
#include <map>
#include <vector>

namespace nff::app {

struct DocumentFileEvent final {
    core::DocumentId documentId{};
    storage::FileWatchEvent event{};
};

class DocumentFileMonitor final {
public:
    void sync(const core::DocumentManager& documents);
    [[nodiscard]] std::vector<DocumentFileEvent> poll();
    void clear() noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct Binding final {
        storage::WatchId watch{};
        std::filesystem::path path;
    };

    storage::FileWatcher watcher_;
    std::map<core::DocumentId, Binding> bindings_;
    std::map<storage::WatchId, core::DocumentId> reverse_;
};

}
