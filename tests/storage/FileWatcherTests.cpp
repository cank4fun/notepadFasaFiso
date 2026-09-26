#include "notepadFasaFiso/storage/FileWatcher.hpp"

#include <filesystem>
#include <fstream>
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

std::filesystem::path tempPath() {
    return std::filesystem::temp_directory_path() / "nff-filewatch-test.txt";
}

void write(const std::filesystem::path& path, const std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void testLifecycle() {
    const auto path = tempPath();
    std::error_code error;
    std::filesystem::remove(path, error);
    write(path, "one\n");

    nff::storage::FileWatcher watcher;
    const auto watched = watcher.watch(path);
    expect(static_cast<bool>(watched), "watch existing file");
    expect(watcher.poll().empty(), "unchanged file emits no event");

    write(path, "two-two\n");
    auto events = watcher.poll();
    expect(events.size() == 1U, "modification emits event");
    if (!events.empty()) {
        expect(events[0].kind == nff::storage::FileWatchEventKind::Modified,
               "modification classified correctly");
    }

    std::filesystem::remove(path, error);
    events = watcher.poll();
    expect(events.size() == 1U, "delete emits event");
    if (!events.empty()) {
        expect(events[0].kind == nff::storage::FileWatchEventKind::Deleted,
               "delete classified correctly");
    }

    write(path, "back\n");
    events = watcher.poll();
    expect(events.size() == 1U, "recreate emits event");
    if (!events.empty()) {
        expect(events[0].kind == nff::storage::FileWatchEventKind::Created,
               "recreate classified correctly");
    }

    std::filesystem::remove(path, error);
}

void testInitiallyMissing() {
    const auto path = std::filesystem::temp_directory_path() / "nff-filewatch-missing.txt";
    std::error_code error;
    std::filesystem::remove(path, error);

    nff::storage::FileWatcher watcher;
    const auto watched = watcher.watch(path);
    expect(static_cast<bool>(watched), "watch missing path for later creation");
    write(path, "created\n");
    const auto events = watcher.poll();
    expect(events.size() == 1U, "creation of watched path emits event");
    if (!events.empty()) {
        expect(events[0].kind == nff::storage::FileWatchEventKind::Created,
               "initially missing path classified as created");
    }
    std::filesystem::remove(path, error);
}

void testAtomicReplacement() {
    const auto path = std::filesystem::temp_directory_path() / "nff-filewatch-replace.txt";
    const auto replacement = std::filesystem::temp_directory_path() / "nff-filewatch-replacement.tmp";
    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(replacement, error);
    write(path, "same-size-a");

    nff::storage::FileWatcher watcher;
    const auto watched = watcher.watch(path);
    expect(static_cast<bool>(watched), "watch file before atomic replacement");

    write(replacement, "same-size-b");
    std::filesystem::rename(replacement, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(replacement, path, error);
    }
    expect(!error, "replacement file moved over watched path");

    const auto events = watcher.poll();
    expect(events.size() == 1U, "atomic replacement emits one event");
    if (!events.empty()) {
        expect(events[0].kind == nff::storage::FileWatchEventKind::Replaced ||
                   events[0].kind == nff::storage::FileWatchEventKind::Modified,
               "atomic replacement is detected");
    }

    std::filesystem::remove(path, error);
    std::filesystem::remove(replacement, error);
}

}

int main() {
    testLifecycle();
    testInitiallyMissing();
    testAtomicReplacement();
    return failures == 0 ? 0 : 1;
}
