#include "BenchmarkSupport.hpp"
#include "notepadFasaFiso/workspace/WorkspaceModel.hpp"
#include "notepadFasaFiso/viewer/ViewCache.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <string_view>

namespace {
std::ptrdiff_t failAfter = -1;
std::size_t allocations = 0;
}

void* operator new(std::size_t size) {
    if (failAfter == 0) throw std::bad_alloc{};
    if (failAfter > 0) --failAfter;
    ++allocations;
    if (auto* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace nff::workspace;

bool valid(const WorkspaceModel& model) {
    const auto snapshot = model.snapshot();
    WorkspaceModel restored;
    if (!restored.restore(snapshot)) return false;
    for (const auto& pane : snapshot.panes) {
        for (auto view : pane.views) {
            if (model.paneContaining(view) != pane.id) return false;
        }
    }
    return !model.paneContaining(ViewId{std::numeric_limits<std::uint64_t>::max()});
}

int main(int argc, char** argv) {
    const std::string_view operation = argc > 1 ? argv[1] : "open";
    if (operation == "cache") {
        nff::bench::TempFixture fixture("nff-allocation-cache");
        const auto path = fixture.path("cache.tmp");
        { std::ofstream out(path, std::ios::binary); out << std::string(16384, 'x'); }
        nff::storage::RandomAccessFile file;
        if (file.open(path)) return 2;
        nff::viewer::ViewCache cache({4096, 8192, 0});
        std::byte byte{};
        for (auto offset : {0U, 4096U}) if (!cache.read(file, offset, {&byte, 1})) return 2;
        const auto before = allocations;
        for (unsigned int i = 0; i < 1000; ++i) {
            if (!cache.read(file, (i % 2) * 4096U, {&byte, 1})) return 2;
        }
        const auto count = allocations - before;
        file.close();
        std::filesystem::remove(path);
        std::cout << "cache hit allocations: " << count << '\n';
        return count == 0 ? 0 : 1;
    }
    if (operation == "cache-miss") {
        nff::bench::TempFixture fixture("nff-allocation-cache-miss");
        const auto path = fixture.path("cache.tmp");
        { std::ofstream out(path, std::ios::binary); out << std::string(16384, 'x'); }
        nff::storage::RandomAccessFile file;
        if (file.open(path)) return 2;
        for (std::ptrdiff_t failure = 0; failure < 8; ++failure) {
            nff::viewer::ViewCache cache({4096, 8192, 0});
            std::byte byte{};
            for (auto offset : {0U, 4096U}) if (!cache.read(file, offset, {&byte, 1})) return 2;
            bool threw = false;
            failAfter = failure;
            try { static_cast<void>(cache.read(file, 8192, {&byte, 1})); }
            catch (const std::bad_alloc&) { threw = true; }
            failAfter = -1;
            if (cache.statistics().residentBlocks != 2 || cache.statistics().residentBytes != 8192) return 1;
            if (threw && cache.statistics().evictions != 0) return 1;
            for (auto offset : {4096U, 8192U, 12288U, 0U}) {
                if (!cache.read(file, offset, {&byte, 1}) || byte != std::byte{'x'}) return 1;
            }
        }
        file.close();
        std::filesystem::remove(path);
        std::cout << "cache-miss: 8 allocation failure positions passed\n";
        return 0;
    }
    for (std::ptrdiff_t failure = 0; failure < 128; ++failure) {
        WorkspaceModel model;
        const auto primary = model.primaryPane();
        const auto target = model.splitPane(primary, SplitOrientation::Horizontal).pane;
        const auto view = model.openView(nff::core::DocumentId{1}, primary);
        const auto snapshot = model.snapshot();
        bool threw = false;
        failAfter = failure;
        try {
            if (operation == "open") static_cast<void>(model.openView(nff::core::DocumentId{2}, primary));
            else if (operation == "move") static_cast<void>(model.moveView(view, target));
            else if (operation == "split") static_cast<void>(model.splitPane(primary, SplitOrientation::Vertical));
            else if (operation == "restore") static_cast<void>(model.restore(snapshot));
            else return 2;
        } catch (const std::bad_alloc&) { threw = true; }
        failAfter = -1;
        if (!valid(model) || (threw && (model.viewCount() != snapshot.views.size() ||
            model.paneCount() != snapshot.panes.size() || model.paneContaining(view) != primary))) {
            std::cerr << operation << " invariant failure at allocation " << failure << '\n';
            return 1;
        }
    }
    std::cout << operation << ": 128 allocation failure positions passed\n";
}
