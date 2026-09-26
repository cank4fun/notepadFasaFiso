#include "BenchmarkSupport.hpp"
#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/core/TextAnalysis.hpp"
#include "notepadFasaFiso/viewer/ViewCache.hpp"
#include "notepadFasaFiso/workspace/WorkspaceModel.hpp"
#include <array>
#include <charconv>
#include <chrono>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#include <fstream>
#endif

namespace {
using Clock = std::chrono::steady_clock;
template<class F> double milliseconds(F&& f) {
    const auto start = Clock::now();
    f();
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
void require(bool value) { if (!value) throw std::runtime_error("benchmark operation failed"); }
void memory(const char* stage) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX info{};
    require(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&info), static_cast<DWORD>(sizeof(info))) != 0);
    std::cout << stage << " rss_bytes=" << info.WorkingSetSize << " peak_rss_bytes=" << info.PeakWorkingSetSize << " private_bytes=" << info.PrivateUsage << '\n';
#else
    rusage info{};
    require(getrusage(RUSAGE_SELF, &info) == 0);
    std::ifstream stat("/proc/self/statm");
    std::uint64_t total{}, resident{};
    stat >> total >> resident;
    std::cout << stage << " rss_bytes=" << resident * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE)) << " peak_rss_bytes=" << static_cast<std::uint64_t>(info.ru_maxrss) * 1024U << '\n';
#endif
}
void hot() {
    nff::core::Document document;
    for (auto size : {1U, 4096U, 4U * 1024U * 1024U}) {
        std::string text(size, 'x');
        for (std::size_t i = 79; i < text.size(); i += 80) text[i] = '\n';
        for (int kind = 0; kind < 3; ++kind) {
            auto sample = text;
            if (kind == 1 && sample.size() >= 2) { sample[0] = '\xc3'; sample[1] = '\xa9'; }
            if (kind == 2) sample.back() = '\xff';
            const auto runs = size > 4096 ? 20U : 10000U;
            double elapsed = 0;
            for (unsigned int i = 0; i < runs; ++i) {
                document.replaceText({});
                elapsed += milliseconds([&] {
                    const auto error = document.applyEdit(0, 0, sample);
                    require(kind == 2 ? error == std::errc::illegal_byte_sequence : !error);
                });
            }
            std::cout << "paste bytes=" << size << " kind=" << kind << " ns=" << elapsed * 1e6 / runs << '\n';
        }
    }
    nff::workspace::WorkspaceModel workspace;
    std::vector<nff::workspace::PaneId> panes{workspace.primaryPane()};
    std::vector<nff::workspace::ViewId> views;
    for (unsigned int i = 1; i < 128; ++i) panes.push_back(workspace.splitPane(panes.back(), nff::workspace::SplitOrientation::Horizontal).pane);
    for (const auto pane : panes) for (unsigned int i = 0; i < 32; ++i) views.push_back(workspace.openView(nff::core::DocumentId{1}, pane));
    std::uint64_t sum = 0;
    std::cout << "paneContaining ns=" << milliseconds([&] { for (unsigned int i = 0; i < 1000000; ++i) sum += workspace.paneContaining(views[i % views.size()])->value; }) * 1e6 / 1000000 << " checksum=" << sum << '\n';
    std::cout << "moveView ns=" << milliseconds([&] { for (unsigned int i = 0; i < 100000; ++i) require(workspace.moveView(views[0], panes[1 + i % 2])); }) * 1e6 / 100000 << '\n';
    std::cout << "closeView ns=" << milliseconds([&] { for (const auto view : views) require(workspace.closeView(view)); }) * 1e6 / static_cast<double>(views.size()) << '\n';
    nff::bench::TempFixture fixture("nff-cache-bench");
    require(!nff::bench::writeRepeatedText(fixture.path("cache.txt"), "cache data\n", 65536));
    nff::storage::RandomAccessFile file;
    require(!file.open(fixture.path("cache.txt")));
    nff::viewer::ViewCache cache({4096, 65536, 0});
    std::array<std::byte, 32> bytes{};
    for (unsigned int i = 0; i < 16; ++i) require(static_cast<bool>(cache.read(file, i * 4096U, bytes)));
    std::cout << "cache_hit ns=" << milliseconds([&] { for (unsigned int i = 0; i < 1000000; ++i) require(static_cast<bool>(cache.read(file, (i % 16) * 4096U, bytes))); }) * 1e6 / 1000000 << '\n';
    std::string text(4U * 1024U * 1024U, 'x');
    std::cout << "analysis_4MiB ms=" << milliseconds([&] { for (int i = 0; i < 20; ++i) sum += nff::core::TextAnalysis::analyzeUtf8(text).longestLineBytes; }) / 20 << " checksum=" << sum << '\n';
}
void large(std::size_t mib, bool save) {
    require(mib == 64 || mib == 256 || mib == 512 || mib == 1024 || mib == 2048);
    const auto size = mib * 1024ULL * 1024ULL;
    require(size <= std::numeric_limits<std::size_t>::max());
    nff::bench::TempFixture fixture("nff-large-editor");
    const auto path = fixture.path("large.txt");
    require(!nff::bench::writeRepeatedText(path, std::string(79, 'x') + '\n', size));
    nff::core::DocumentManager manager;
    const auto opened = manager.open(path);
    require(static_cast<bool>(opened));
    std::cout << "core-only MiB=" << mib << '\n'; memory("before");
    std::cout << "materialize_ms=" << milliseconds([&] { require(!manager.materializeForEdit(opened.id, static_cast<std::size_t>(size))); }) << '\n';
    memory("loaded");
    auto* document = manager.get(opened.id);
    const auto offset = document->text().size() / 2;
    std::cout << "replace_middle_ms=" << milliseconds([&] { require(!document->applyEdit(offset, 1, "z")); }) << '\n';
    std::cout << "newline_insert_ms=" << milliseconds([&] { require(!document->applyEdit(offset, 0, "\n")); }) << '\n';
    std::cout << "newline_remove_ms=" << milliseconds([&] { require(!document->applyEdit(offset, 1, "")); }) << '\n';
    memory("edited");
    if (save) { std::cout << "save_ms=" << milliseconds([&] { require(!document->save()); }) << '\n'; memory("saved"); }
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 1) hot();
        else if ((argc == 3 || argc == 4) && std::string_view(argv[1]) == "--large") {
            std::size_t mib{}; const std::string_view value(argv[2]);
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), mib);
            require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size());
            require(argc != 4 || std::string_view(argv[3]) == "--save");
            large(mib, argc == 4);
        } else throw std::runtime_error("usage: nff_hot_path_bench [--large 64|256|512|1024|2048 [--save]]");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
