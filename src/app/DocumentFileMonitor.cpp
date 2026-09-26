#include "notepadFasaFiso/app/DocumentFileMonitor.hpp"

#include <set>

namespace nff::app {

void DocumentFileMonitor::sync(const core::DocumentManager& documents) {
    std::set<core::DocumentId> live;

    for (const auto documentId : documents.ids()) {
        const auto* document = documents.get(documentId);
        if (document == nullptr || document->path().empty()) {
            continue;
        }
        live.insert(documentId);

        const auto existing = bindings_.find(documentId);
        if (existing != bindings_.end() && existing->second.path == document->path()) {
            continue;
        }
        if (existing != bindings_.end()) {
            static_cast<void>(watcher_.unwatch(existing->second.watch));
            reverse_.erase(existing->second.watch);
            bindings_.erase(existing);
        }

        const auto watched = watcher_.watch(document->path());
        if (!watched) {
            continue;
        }
        bindings_.emplace(documentId, Binding{watched.id, document->path()});
        reverse_.emplace(watched.id, documentId);
    }

    for (auto iterator = bindings_.begin(); iterator != bindings_.end();) {
        if (!live.contains(iterator->first)) {
            static_cast<void>(watcher_.unwatch(iterator->second.watch));
            reverse_.erase(iterator->second.watch);
            iterator = bindings_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

std::vector<DocumentFileEvent> DocumentFileMonitor::poll() {
    std::vector<DocumentFileEvent> result;
    const auto events = watcher_.poll();
    result.reserve(events.size());
    for (const auto& event : events) {
        const auto binding = reverse_.find(event.id);
        if (binding != reverse_.end()) {
            result.push_back({binding->second, event});
        }
    }
    return result;
}

void DocumentFileMonitor::clear() noexcept {
    watcher_.clear();
    bindings_.clear();
    reverse_.clear();
}

std::size_t DocumentFileMonitor::size() const noexcept {
    return bindings_.size();
}

}
