#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/storage/FileWriter.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] std::filesystem::path tempRoot() {
    auto root = std::filesystem::temp_directory_path() / "nff-content-routing";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    require(!error, "create routing temp root");
    return root;
}

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view text) {
    const auto* first = reinterpret_cast<const std::byte*>(text.data());
    return {first, first + text.size()};
}

void testTextRoutesWithoutMaterializingLargeContent() {
    const auto root = tempRoot();
    const auto path = root / "large.log";
    constexpr std::string_view content = "alpha\nbeta\ngamma\n";
    auto error = nff::storage::FileWriter::writeAtomically(path, bytes(content));
    require(!error, "write routed text fixture");

    nff::core::InspectOptions options;
    options.viewerSizeThreshold = 1U;

    nff::core::DocumentManager documents;
    const auto opened = documents.openRouted(path, options);
    require(static_cast<bool>(opened), "open routed viewer document");
    auto* document = documents.get(opened.id);
    require(document != nullptr, "routed document exists");
    require(document->profile().recommendedMode == nff::core::OpenMode::Viewer,
            "large text routes to viewer");
    require(!document->textBufferLoaded(), "viewer route does not materialize text buffer");
    require(document->text().empty(), "viewer route keeps canonical text buffer empty");
    require(document->profile().fileSize == content.size(), "viewer retains inspected file size");

    const auto copyError = document->saveCopy(root / "must-not-be-empty.txt");
    require(copyError == std::make_error_code(std::errc::operation_not_supported),
            "reference document cannot save an empty synthetic copy");

    error = documents.reloadRouted(opened.id, nff::core::InspectOptions{});
    require(!error, "reload routed document as editor");
    document = documents.get(opened.id);
    require(document != nullptr && document->textBufferLoaded(),
            "editor reload materializes canonical text");
    require(document->profile().recommendedMode == nff::core::OpenMode::Editor,
            "editor reload updates routed mode");
    require(document->text() == content, "editor reload preserves file content");

    std::filesystem::remove_all(root, error);
}

void testExplicitEditorAndViewerTransitions() {
    const auto root = tempRoot();
    const auto path = root / "transition.log";
    constexpr std::string_view content = "one\ntwo\nthree\n";
    auto error = nff::storage::FileWriter::writeAtomically(path, bytes(content));
    require(!error, "write transition fixture");

    nff::core::InspectOptions options;
    options.viewerSizeThreshold = 1U;

    nff::core::DocumentManager documents;
    const auto opened = documents.openRouted(path, options);
    require(static_cast<bool>(opened), "open transition fixture as viewer");
    auto* document = documents.get(opened.id);
    require(document != nullptr && !document->textBufferLoaded(),
            "transition fixture starts as reference viewer");

    error = documents.materializeForEdit(opened.id, content.size());
    require(!error, "edit anyway materializes full text");
    document = documents.get(opened.id);
    require(document != nullptr && document->textBufferLoaded(),
            "materialized document owns editable buffer");
    require(document != nullptr && document->profile().recommendedMode == nff::core::OpenMode::Editor,
            "materialized document becomes editor mode");
    require(document != nullptr && document->text() == content,
            "edit anyway preserves full file content");

    document->markModified();
    error = documents.dematerializeForViewer(opened.id, options);
    require(error == std::make_error_code(std::errc::text_file_busy),
            "viewer transition refuses to discard modified text implicitly");
    error = documents.dematerializeForViewer(opened.id, options, true);
    require(!error, "explicit discard can release modified editor buffer into viewer mode");
    document = documents.get(opened.id);
    require(document != nullptr && !document->textBufferLoaded(),
            "viewer transition releases canonical text buffer");
    require(document != nullptr && document->profile().recommendedMode == nff::core::OpenMode::Viewer,
            "explicit viewer transition forces scalable mode even for manual choice");

    std::filesystem::remove_all(root, error);
}

void testBinaryRoutesToReferenceHexPreview() {
    const auto root = tempRoot();
    const auto path = root / "sample.bin";
    std::vector<std::byte> payload(4096U, std::byte{0});
    payload[7] = std::byte{0xFF};
    auto error = nff::storage::FileWriter::writeAtomically(path, payload);
    require(!error, "write binary routing fixture");

    nff::core::DocumentManager documents;
    const auto opened = documents.open(path);
    require(static_cast<bool>(opened), "open routed binary document");
    const auto* document = documents.get(opened.id);
    require(document != nullptr, "binary routed document exists");
    require(document->profile().recommendedMode == nff::core::OpenMode::BinaryPreview,
            "binary content routes to hex preview");
    require(!document->textBufferLoaded(), "binary route does not allocate text buffer");
    require(document->profile().encoding.binaryLike, "binary profile retained");

    std::filesystem::remove_all(root, error);
}

void testPreparedProfileHonorsExplicitStartupMode() {
    const auto root = tempRoot();
    const auto path = root / "explicit-mode.txt";
    constexpr std::string_view content = "first\nsecond\nthird\n";
    auto error = nff::storage::FileWriter::writeAtomically(path, bytes(content));
    require(!error, "write prepared startup mode fixture");

    const auto inspected = nff::core::FileSniffer::inspect(path);
    require(static_cast<bool>(inspected), "inspect prepared startup mode fixture");

    {
        nff::core::DocumentManager documents;
        auto profile = inspected.profile;
        profile.recommendedMode = nff::core::OpenMode::Viewer;
        const auto opened = documents.openPrepared(path, profile);
        require(static_cast<bool>(opened), "prepared viewer target opens");
        const auto* document = documents.get(opened.id);
        require(document != nullptr && !document->textBufferLoaded(),
                "explicit prepared viewer avoids materializing small text");
        require(document != nullptr &&
                    document->profile().recommendedMode == nff::core::OpenMode::Viewer,
                "explicit prepared viewer mode is preserved");
    }

    {
        nff::core::InspectOptions options;
        options.viewerSizeThreshold = 1U;
        const auto largeProfile = nff::core::FileSniffer::inspect(path, options);
        require(static_cast<bool>(largeProfile), "inspect fixture as automatic viewer");
        auto profile = largeProfile.profile;
        profile.recommendedMode = nff::core::OpenMode::Editor;

        nff::core::DocumentManager documents;
        const auto opened = documents.openPrepared(path, profile, content.size());
        require(static_cast<bool>(opened), "prepared editor target opens");
        const auto* document = documents.get(opened.id);
        require(document != nullptr && document->textBufferLoaded(),
                "explicit prepared editor materializes text despite viewer threshold");
        require(document != nullptr && document->text() == content,
                "prepared editor retains complete text content");
    }

    std::filesystem::remove_all(root, error);
}

void testUndecodableTextFallsBackToHexPreview() {
    const auto root = tempRoot();

    struct Fixture final {
        std::string_view name;
        std::vector<std::byte> payload;
    };

    auto unknown8Bit = bytes("caf");
    unknown8Bit.push_back(std::byte{0xE9});
    unknown8Bit.push_back(std::byte{0x0A});

    const std::vector<Fixture> fixtures{
        {"unknown-8bit.txt", unknown8Bit},
        {"malformed-utf8-bom.txt",
         {std::byte{0xEF}, std::byte{0xBB}, std::byte{0xBF}, std::byte{0xC3}, std::byte{0x28}}},
        {"malformed-utf16le-bom.txt",
         {std::byte{0xFF}, std::byte{0xFE}, std::byte{0x00}, std::byte{0xD8},
          std::byte{0x41}, std::byte{0x00}}},
        {"malformed-utf32le-bom.txt",
         {std::byte{0xFF}, std::byte{0xFE}, std::byte{0x00}, std::byte{0x00},
          std::byte{0x00}, std::byte{0xD8}, std::byte{0x00}, std::byte{0x00}}},
    };

    for (const auto& fixture : fixtures) {
        const auto path = root / fixture.name;
        auto error = nff::storage::FileWriter::writeAtomically(path, fixture.payload);
        require(!error, "write undecodable routing fixture");

        const auto inspected = nff::core::FileSniffer::inspect(path);
        require(static_cast<bool>(inspected), "inspect undecodable routing fixture");
        require(inspected.profile.recommendedMode == nff::core::OpenMode::BinaryPreview,
                "undecodable text falls back to safe hex preview");

        nff::core::DocumentManager documents;
        const auto opened = documents.open(path);
        require(static_cast<bool>(opened), "undecodable routing fixture still opens safely");
        const auto* document = documents.get(opened.id);
        require(document != nullptr && !document->textBufferLoaded(),
                "undecodable fallback never materializes an unsafe editor buffer");
        require(document != nullptr &&
                    document->profile().recommendedMode == nff::core::OpenMode::BinaryPreview,
                "undecodable fallback retains hex preview mode");
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testFinalVlcOpenMatrixContentWinsOverNames() {
    const auto root = tempRoot();

    struct Fixture final {
        std::string_view name;
        std::vector<std::byte> payload;
        nff::core::OpenMode expectedMode;
        nff::core::LineEnding expectedLineEnding{nff::core::LineEnding::Unknown};
    };

    const std::vector<Fixture> fixtures{
        {"empty.weird", {}, nff::core::OpenMode::Editor},
        {"extensionless", bytes("plain text\n"), nff::core::OpenMode::Editor,
         nff::core::LineEnding::LF},
        {"misleading.bin", bytes("still ordinary text\n"), nff::core::OpenMode::Editor,
         nff::core::LineEnding::LF},
        {"mixed.data", bytes("a\r\nb\nc\r"), nff::core::OpenMode::Editor,
         nff::core::LineEnding::Mixed},
    };

    for (const auto& fixture : fixtures) {
        const auto path = root / fixture.name;
        const auto error = nff::storage::FileWriter::writeAtomically(path, fixture.payload);
        require(!error, "write final VLC routing fixture");
        const auto inspected = nff::core::FileSniffer::inspect(path);
        require(static_cast<bool>(inspected), "inspect final VLC routing fixture");
        require(inspected.profile.recommendedMode == fixture.expectedMode,
                "final VLC routing follows content rather than extension");
        if (fixture.expectedLineEnding != nff::core::LineEnding::Unknown) {
            require(inspected.profile.lineEnding == fixture.expectedLineEnding,
                    "final VLC routing preserves newline analysis");
        }
    }

    const auto longPath = root / "long-single-line.log";
    const auto error = nff::storage::FileWriter::writeAtomically(
        longPath, bytes("0123456789abcdef"));
    require(!error, "write final VLC long-line fixture");
    nff::core::InspectOptions options;
    options.longLineThreshold = 8U;
    options.viewerSizeThreshold = 1024U;
    options.sampleBytes = 1024U;
    const auto longInspected = nff::core::FileSniffer::inspect(longPath, options);
    require(static_cast<bool>(longInspected) &&
                longInspected.profile.recommendedMode == nff::core::OpenMode::Viewer,
            "final VLC routing sends expensive long lines to scalable Viewer");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

}

int main() {
    testTextRoutesWithoutMaterializingLargeContent();
    testExplicitEditorAndViewerTransitions();
    testBinaryRoutesToReferenceHexPreview();
    testPreparedProfileHonorsExplicitStartupMode();
    testUndecodableTextFallsBackToHexPreview();
    testFinalVlcOpenMatrixContentWinsOverNames();
    std::cout << "content routing tests passed\n";
    return EXIT_SUCCESS;
}
