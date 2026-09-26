#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/recovery/AutoSaveManager.hpp"
#include "notepadFasaFiso/recovery/RecoveryManager.hpp"
#include "notepadFasaFiso/storage/FileReader.hpp"
#include "notepadFasaFiso/storage/FileWriter.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view text) {
    const auto* begin = reinterpret_cast<const std::byte*>(text.data());
    return {begin, begin + text.size()};
}

[[nodiscard]] std::filesystem::path freshTempDirectory(const std::string_view name) {
    const auto root = std::filesystem::temp_directory_path() / std::string(name);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    expect(!error, "create recovery test directory");
    return root;
}

template <typename Integer>
void appendLegacyLittleEndian(std::vector<std::byte>& output, Integer value) {
    using Unsigned = std::make_unsigned_t<Integer>;
    auto current = static_cast<std::uint64_t>(static_cast<Unsigned>(value));
    for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
        output.push_back(static_cast<std::byte>(current & 0xFFU));
        current >>= 8U;
    }
}

void testLegacyV1RecoveryStillLoads() {
    const auto root = freshTempDirectory("nff-recovery-v1-compat");
    const auto recoveryRoot = root / "recovery";
    std::error_code error;
    std::filesystem::create_directories(recoveryRoot, error);
    expect(!error, "create legacy recovery root");

    nff::recovery::RecoveryManager recovery(recoveryRoot, 4242U);
    const nff::core::DocumentId id{17U};
    const std::string_view text = "legacy recovery\n";

    const std::array<std::byte, 8> magic{
        std::byte{'N'}, std::byte{'F'}, std::byte{'F'}, std::byte{'R'},
        std::byte{'E'}, std::byte{'C'}, std::byte{'0'}, std::byte{'1'}};
    std::vector<std::byte> payload;
    payload.insert(payload.end(), magic.begin(), magic.end());
    appendLegacyLittleEndian(payload, std::uint32_t{1U});
    appendLegacyLittleEndian(payload, id.value);
    appendLegacyLittleEndian(payload, std::uint64_t{123U});
    appendLegacyLittleEndian(payload, static_cast<std::uint8_t>(nff::encoding::Encoding::Utf8));
    appendLegacyLittleEndian(payload, static_cast<std::uint8_t>(nff::core::LineEnding::LF));
    appendLegacyLittleEndian(payload, std::uint8_t{0U});
    appendLegacyLittleEndian(payload, std::uint8_t{0U});
    appendLegacyLittleEndian(payload, std::uint64_t{0U});
    appendLegacyLittleEndian(payload, static_cast<std::uint64_t>(text.size()));
    const auto* textBytes = reinterpret_cast<const std::byte*>(text.data());
    payload.insert(payload.end(), textBytes, textBytes + text.size());

    expect(!nff::storage::FileWriter::writeAtomically(recovery.snapshotPath(id), payload),
           "write legacy v1 recovery fixture");
    const auto loaded = recovery.load(recovery.snapshotPath(id));
    expect(static_cast<bool>(loaded), "legacy v1 recovery loads");
    if (loaded) {
        expect(loaded.snapshot.text == text, "legacy v1 text preserved");
        expect(loaded.snapshot.appearance.empty(),
               "legacy v1 recovery defaults to empty appearance");
    }

    std::filesystem::remove_all(root, error);
}

void testRecoveryRoundTrip() {
    const auto root = freshTempDirectory("nff-recovery-roundtrip");
    nff::core::DocumentManager documents;
    const auto id = documents.createUntitled();
    auto* document = documents.get(id);
    expect(document != nullptr, "untitled document exists");
    if (document == nullptr) {
        return;
    }

    document->replaceText("unsaved Türkçe 🚀\nsecond line\n");
    nff::metadata::TextAppearanceMap appearance;
    const auto family = appearance.internFontFamily("DejaVu Sans Mono");
    expect(family.has_value(), "intern recovery appearance font");
    appearance.setForeground(0U, 7U, 0xFF3366CCU);
    if (family.has_value()) {
        expect(appearance.setFontFamily(0U, 7U, *family),
               "set recovery appearance font");
    }
    expect(appearance.setFontSize(0U, 7U, 16U),
           "set recovery appearance size");
    appearance.setSpoiler(8U, 15U, true);

    nff::recovery::RecoveryManager recovery(root / "recovery", 12345);
    const auto error = recovery.checkpoint(id, *document, &appearance);
    expect(!error, "write recovery snapshot");

    std::error_code listError;
    const auto snapshots = recovery.list(listError);
    expect(!listError, "list recovery snapshots");
    expect(snapshots.size() == 1, "one recovery snapshot listed");
    if (snapshots.empty()) {
        return;
    }

    const auto loaded = recovery.load(snapshots.front());
    expect(static_cast<bool>(loaded), "load recovery snapshot");
    if (loaded) {
        expect(loaded.snapshot.documentId == id, "recovery document id preserved");
        expect(loaded.snapshot.originalPath.empty(), "untitled recovery path remains empty");
        expect(loaded.snapshot.text == document->text(), "recovery text round trip");
        expect(loaded.snapshot.encoding == document->saveEncoding(),
               "recovery encoding preserved");
        expect(loaded.snapshot.appearance.fontFamilies() == appearance.fontFamilies(),
               "recovery appearance font table preserved");
        expect(loaded.snapshot.appearance.spans() == appearance.spans(),
               "recovery selection appearance preserved");
    }

    document->markClean();
    expect(!recovery.checkpoint(id, *document), "clean checkpoint discards recovery");
    const auto afterDiscard = recovery.list(listError);
    expect(!listError && afterDiscard.empty(), "clean document recovery removed");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testDiscardSpecificRecoverySnapshotIsRootScoped() {
    const auto root = freshTempDirectory("nff-recovery-discard-specific");
    nff::core::DocumentManager documents;
    const auto id = documents.createUntitled();
    auto* document = documents.get(id);
    expect(document != nullptr, "discard-specific document exists");
    if (document == nullptr) {
        return;
    }
    document->replaceText("recover me\n");

    nff::recovery::RecoveryManager recovery(root / "recovery", 321U);
    expect(!recovery.checkpoint(id, *document), "discard-specific checkpoint saves");
    const auto snapshot = recovery.snapshotPath(id);
    expect(std::filesystem::exists(snapshot), "discard-specific snapshot exists");
    expect(!recovery.discardSnapshot(snapshot), "listed recovery snapshot can be discarded");
    expect(!std::filesystem::exists(snapshot), "discard-specific snapshot is removed");

    const auto outsider = root / "outside.nff-recovery";
    expect(!nff::storage::FileWriter::writeAtomically(outsider, bytes("not a recovery")),
           "write outside recovery-like file");
    expect(recovery.discardSnapshot(outsider) ==
               std::make_error_code(std::errc::invalid_argument),
           "discardSnapshot refuses paths outside the recovery root");
    expect(std::filesystem::exists(outsider),
           "rejected outside recovery-like file is untouched");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testLoadSpecificRecoverySnapshotIsRootScoped() {
    const auto root = freshTempDirectory("nff-recovery-load-specific");
    nff::core::DocumentManager documents;
    const auto id = documents.createUntitled();
    auto* document = documents.get(id);
    expect(document != nullptr, "load-specific document exists");
    if (document == nullptr) {
        return;
    }
    document->replaceText("outside recovery payload\n");

    nff::recovery::RecoveryManager recovery(root / "recovery", 322U);
    nff::recovery::RecoveryManager outsider(root / "outside-recovery", 777U);
    expect(!outsider.checkpoint(id, *document), "outside recovery checkpoint saves");

    const auto outsideSnapshot = outsider.snapshotPath(id);
    expect(std::filesystem::exists(outsideSnapshot), "outside recovery snapshot exists");

    const auto loaded = recovery.load(outsideSnapshot);
    expect(!loaded, "load refuses recovery snapshots outside the configured root");
    expect(loaded.error == std::make_error_code(std::errc::invalid_argument),
           "outside recovery load reports invalid_argument");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testExternalChangeProtection() {
    const auto root = freshTempDirectory("nff-external-change");
    const auto path = root / "notes.txt";
    expect(!nff::storage::FileWriter::writeAtomically(path, bytes("original\n")),
           "write original file");

    nff::core::Document document;
    expect(!document.load(path), "load external-change document");
    document.replaceText("local edit\n");

    expect(!nff::storage::FileWriter::writeAtomically(path, bytes("external edit changed size\n")),
           "write external modification");
    expect(document.externalChangeState() == nff::storage::FileChangeState::Modified,
           "external modification detected");

    const auto blocked = document.save();
    expect(blocked == std::make_error_code(std::errc::text_file_busy),
           "normal save blocks external overwrite");
    expect(document.modified(), "blocked save keeps local document dirty");

    nff::core::SaveOptions overwrite;
    overwrite.allowExternalOverwrite = true;
    expect(!document.save(overwrite), "explicit external overwrite succeeds");
    expect(document.externalChangeState() == nff::storage::FileChangeState::Unchanged,
           "disk state refreshes after overwrite");

    const auto read = nff::storage::FileReader::readAll(path, 1024);
    expect(static_cast<bool>(read), "read overwritten file");
    if (read) {
        const std::string_view value(reinterpret_cast<const char*>(read.bytes.data()),
                                     read.bytes.size());
        expect(value == "local edit\n", "explicit overwrite wrote local contents");
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testExternalDeletionProtection() {
    const auto root = freshTempDirectory("nff-external-delete");
    const auto path = root / "deleted.txt";
    expect(!nff::storage::FileWriter::writeAtomically(path, bytes("base\n")),
           "write deletion fixture");

    nff::core::Document document;
    expect(!document.load(path), "load deletion fixture");
    document.replaceText("local edit survives\n");

    std::error_code removeError;
    std::filesystem::remove(path, removeError);
    expect(!removeError, "delete document externally");
    expect(document.externalChangeState() == nff::storage::FileChangeState::Deleted,
           "external deletion detected");
    expect(document.save() == std::make_error_code(std::errc::no_such_file_or_directory),
           "normal save does not silently recreate externally deleted file");
    expect(document.modified(), "external deletion keeps local edits dirty");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testAutoSaveRecoveryAndFileSave() {
    const auto root = freshTempDirectory("nff-autosave");
    const auto path = root / "autosave.txt";
    expect(!nff::storage::FileWriter::writeAtomically(path, bytes("start\n")),
           "write autosave fixture");

    nff::core::DocumentManager documents;
    const auto opened = documents.open(path);
    expect(static_cast<bool>(opened), "open autosave fixture");
    if (!opened) {
        return;
    }

    auto* document = documents.get(opened.id);
    expect(document != nullptr, "autosave document exists");
    if (document == nullptr) {
        return;
    }

    nff::recovery::RecoveryManager recovery(root / "recovery", 999);
    nff::metadata::TextAppearanceMap recoveryAppearance;
    recoveryAppearance.setForeground(0U, 6U, 0xFFAA5500U);
    recovery.setAppearanceProvider(
        [&recoveryAppearance, documentId = opened.id](const nff::core::DocumentId candidate)
            -> const nff::metadata::TextAppearanceMap* {
            return candidate == documentId ? &recoveryAppearance : nullptr;
        });
    nff::recovery::AutoSavePolicy policy;
    policy.recoveryDelay = std::chrono::milliseconds(100);
    policy.saveRealFiles = true;
    policy.saveDelay = std::chrono::milliseconds(500);
    nff::recovery::AutoSaveManager autosave(documents, recovery, policy);

    const auto start = nff::recovery::AutoSaveManager::Clock::time_point{};
    document->replaceText("edited\n");
    autosave.noteEdited(opened.id, start);

    auto results = autosave.poll(start + std::chrono::milliseconds(150));
    expect(results.size() == 1, "recovery poll emits one action");
    if (!results.empty()) {
        expect(results.front().outcome == nff::recovery::AutoSaveOutcome::RecoveryCheckpoint,
               "inactivity creates recovery checkpoint");
    }
    expect(std::filesystem::exists(recovery.snapshotPath(opened.id)),
           "recovery file exists before real autosave");
    const auto appearanceSnapshot = recovery.load(recovery.snapshotPath(opened.id));
    expect(static_cast<bool>(appearanceSnapshot),
           "autosave recovery snapshot with appearance loads");
    if (appearanceSnapshot) {
        expect(appearanceSnapshot.snapshot.appearance.spans() ==
                   recoveryAppearance.spans(),
               "autosave recovery uses live appearance provider");
    }

    results = autosave.poll(start + std::chrono::milliseconds(600));
    expect(results.size() == 1, "autosave poll emits one action");
    if (!results.empty()) {
        expect(results.front().outcome == nff::recovery::AutoSaveOutcome::FileSaved,
               "real file autosaves after delay");
    }
    expect(!document->modified(), "autosave marks document clean");
    expect(!std::filesystem::exists(recovery.snapshotPath(opened.id)),
           "successful autosave clears recovery snapshot");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testAutoSaveConflictKeepsRecovery() {
    const auto root = freshTempDirectory("nff-autosave-conflict");
    const auto path = root / "conflict.txt";
    expect(!nff::storage::FileWriter::writeAtomically(path, bytes("base\n")),
           "write conflict fixture");

    nff::core::DocumentManager documents;
    const auto opened = documents.open(path);
    expect(static_cast<bool>(opened), "open conflict fixture");
    if (!opened) {
        return;
    }

    auto* document = documents.get(opened.id);
    expect(document != nullptr, "conflict document exists");
    if (document == nullptr) {
        return;
    }

    nff::recovery::RecoveryManager recovery(root / "recovery", 555);
    nff::recovery::AutoSavePolicy policy;
    policy.recoveryDelay = std::chrono::milliseconds(50);
    policy.saveRealFiles = true;
    policy.saveDelay = std::chrono::milliseconds(100);
    nff::recovery::AutoSaveManager autosave(documents, recovery, policy);

    const auto start = nff::recovery::AutoSaveManager::Clock::time_point{};
    document->replaceText("local unsaved\n");
    autosave.noteEdited(opened.id, start);
    expect(!nff::storage::FileWriter::writeAtomically(path, bytes("external changed content\n")),
           "write conflicting external content");

    const auto results = autosave.poll(start + std::chrono::milliseconds(200));
    expect(results.size() == 1, "conflict poll emits one action");
    if (!results.empty()) {
        expect(results.front().outcome == nff::recovery::AutoSaveOutcome::ExternalConflict,
               "autosave reports external conflict");
    }
    expect(document->modified(), "conflict preserves local dirty state");
    expect(std::filesystem::exists(recovery.snapshotPath(opened.id)),
           "conflict writes recovery snapshot");

    const auto read = nff::storage::FileReader::readAll(path, 1024);
    expect(static_cast<bool>(read), "read external file after conflict");
    if (read) {
        const std::string_view value(reinterpret_cast<const char*>(read.bytes.data()),
                                     read.bytes.size());
        expect(value == "external changed content\n", "autosave does not overwrite conflict");
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testSameSizeSameMtimeReplacementBlocksSave() {
    const auto root = freshTempDirectory("nff-external-identity-replace");
    const auto path = root / "identity.txt";
    const auto replacement = root / "replacement.tmp";
    expect(!nff::storage::FileWriter::writeAtomically(path, bytes("AAAA\n")),
           "write identity replacement baseline");

    std::error_code error;
    const auto baselineTime = std::filesystem::last_write_time(path, error);
    expect(!error, "capture identity baseline mtime");

    nff::core::Document document;
    expect(!document.load(path), "load identity replacement document");
    document.replaceText("LOCAL\n");

    expect(!nff::storage::FileWriter::writeAtomically(replacement, bytes("BBBB\n")),
           "write same-size replacement");
    std::filesystem::last_write_time(replacement, baselineTime, error);
    expect(!error, "restore replacement mtime before rename");
    std::filesystem::rename(replacement, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(replacement, path, error);
    }
    expect(!error, "replace backing file with same size and mtime");
    std::filesystem::last_write_time(path, baselineTime, error);
    expect(!error, "restore replaced path mtime");

    expect(document.externalChangeState() == nff::storage::FileChangeState::Modified,
           "file identity detects replacement even when size and mtime match");
    const auto saveError = document.save();
    expect(saveError == std::make_error_code(std::errc::text_file_busy),
           "normal save refuses same-size same-mtime replacement");
    expect(document.modified(), "blocked identity-conflict save keeps local text dirty");

    const auto read = nff::storage::FileReader::readAll(path, 1024U);
    expect(static_cast<bool>(read), "read identity replacement after blocked save");
    if (read) {
        const std::string_view value(reinterpret_cast<const char*>(read.bytes.data()),
                                     read.bytes.size());
        expect(value == "BBBB\n", "blocked identity conflict leaves external bytes untouched");
    }

    std::filesystem::remove_all(root, error);
}

#ifndef _WIN32
void testSavingSymlinkRefusesToReplaceLink() {
    const auto root = freshTempDirectory("nff-symlink-save-safety");
    const auto target = root / "target.txt";
    const auto link = root / "link.txt";
    expect(!nff::storage::FileWriter::writeAtomically(target, bytes("target\n")),
           "write symlink save target");

    std::error_code error;
    std::filesystem::create_symlink(target.filename(), link, error);
    expect(!error, "create relative symlink save fixture");
    if (error) {
        std::filesystem::remove_all(root, error);
        return;
    }

    nff::core::Document document;
    expect(!document.load(link), "load document through symlink");
    document.replaceText("local through link\n");
    const auto saveError = document.save();
    expect(saveError == std::make_error_code(std::errc::operation_not_supported),
           "normal save refuses to replace a symlink path");
    expect(document.modified(), "symlink save refusal keeps local text dirty");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(link, error)),
           "failed symlink save preserves the link itself");

    const auto read = nff::storage::FileReader::readAll(target, 1024U);
    expect(static_cast<bool>(read), "read symlink target after refused save");
    if (read) {
        const std::string_view value(reinterpret_cast<const char*>(read.bytes.data()),
                                     read.bytes.size());
        expect(value == "target\n", "refused symlink save leaves target bytes untouched");
    }

    std::filesystem::remove_all(root, error);
}
#endif

}

int main() {
    testLegacyV1RecoveryStillLoads();
    testRecoveryRoundTrip();
    testDiscardSpecificRecoverySnapshotIsRootScoped();
    testLoadSpecificRecoverySnapshotIsRootScoped();
    testExternalChangeProtection();
    testExternalDeletionProtection();
    testAutoSaveRecoveryAndFileSave();
    testAutoSaveConflictKeepsRecovery();
    testSameSizeSameMtimeReplacementBlocksSave();
#ifndef _WIN32
    testSavingSymlinkRefusesToReplaceLink();
#endif

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "all recovery tests passed\n";
    return 0;
}