#include "notepadFasaFiso/history/RecentFiles.hpp"
#include "notepadFasaFiso/history/Breadcrumbs.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::filesystem::path tempPath(const std::string_view name) {
    return std::filesystem::temp_directory_path() /
           ("nff-recent-test-" + std::string(name) + ".nff");
}

void testOrderingAndCapacity() {
    nff::history::RecentFiles recent(2U);
    recent.touch("one.txt", nff::core::OpenMode::Editor, 100U);
    recent.touch("two.txt", nff::core::OpenMode::Viewer, 200U);
    recent.touch("three.txt", nff::core::OpenMode::BinaryPreview, 300U);

    expect(recent.size() == 2U, "recent list respects capacity");
    expect(recent.entries()[0].path == std::filesystem::path("three.txt"),
           "newest entry is first");
    expect(recent.entries()[1].path == std::filesystem::path("two.txt"),
           "oldest entry is evicted");

    recent.touch("two.txt", nff::core::OpenMode::Editor, 400U);
    expect(recent.entries()[0].path == std::filesystem::path("two.txt"),
           "touch moves existing entry to front");
    expect(recent.entries()[0].openCount == 2U, "touch increments open count");
    expect(recent.entries()[0].lastOpenMode == nff::core::OpenMode::Editor,
           "touch updates open mode");
}

void testPersistence() {
    const auto path = tempPath("roundtrip");
    std::error_code error;
    std::filesystem::remove(path, error);

    nff::history::RecentFiles recent;
    recent.touch("alpha.txt", nff::core::OpenMode::Editor, 1000U);
    recent.touch("beta.log", nff::core::OpenMode::Viewer, 2000U);
    expect(!nff::history::Breadcrumbs::save(path, recent), "recent store saves");

    auto loaded = nff::history::Breadcrumbs::load(path);
    expect(static_cast<bool>(loaded), "recent store loads");
    expect(loaded.recent.size() == 2U, "roundtrip preserves count");
    expect(loaded.recent.entries()[0].path == std::filesystem::path("beta.log"),
           "roundtrip preserves ordering");
    expect(loaded.recent.entries()[0].lastOpenMode == nff::core::OpenMode::Viewer,
           "roundtrip preserves mode");

    std::filesystem::remove(path, error);
}

}

int main() {
    testOrderingAndCapacity();
    testPersistence();
    return failures == 0 ? 0 : 1;
}
