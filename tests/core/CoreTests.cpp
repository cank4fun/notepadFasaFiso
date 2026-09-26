#include "notepadFasaFiso/core/Document.hpp"
#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/core/FileSniffer.hpp"
#include "notepadFasaFiso/core/TextAnalysis.hpp"
#include "notepadFasaFiso/encoding/EncodingDetector.hpp"
#include "notepadFasaFiso/encoding/TextCodec.hpp"
#include "notepadFasaFiso/storage/FileReader.hpp"
#include "notepadFasaFiso/storage/FileWriter.hpp"
#include "notepadFasaFiso/viewer/LargeFileViewer.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
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
    expect(!error, "create temporary test directory");
    return root;
}

void testEncodingDetection() {
    const auto ascii = bytes("hello\nworld\n");
    const auto asciiResult = nff::encoding::EncodingDetector::detect(ascii);
    expect(asciiResult.encoding == nff::encoding::Encoding::Ascii, "ASCII detection");
    expect(!asciiResult.binaryLike, "ASCII must not be binary");

    const std::array<std::byte, 5> utf8Bom{
        std::byte{0xEF}, std::byte{0xBB}, std::byte{0xBF}, std::byte{'x'}, std::byte{'\n'}};
    const auto bomResult = nff::encoding::EncodingDetector::detect(utf8Bom);
    expect(bomResult.encoding == nff::encoding::Encoding::Utf8, "UTF-8 BOM detection");
    expect(bomResult.hasBom, "UTF-8 BOM flag");

    const std::array<std::byte, 8> utf16LeNoBom{
        std::byte{'h'}, std::byte{0x00}, std::byte{'i'}, std::byte{0x00},
        std::byte{'\n'}, std::byte{0x00}, std::byte{'x'}, std::byte{0x00}};
    const auto utf16Result = nff::encoding::EncodingDetector::detect(utf16LeNoBom);
    expect(utf16Result.encoding == nff::encoding::Encoding::Utf16LE,
           "BOM-less UTF-16 LE detection");
    expect(!utf16Result.binaryLike, "BOM-less UTF-16 must not be binary");

    const std::array<std::byte, 4> binary{
        std::byte{'a'}, std::byte{0x00}, std::byte{'b'}, std::byte{'c'}};
    const auto binaryResult = nff::encoding::EncodingDetector::detect(binary);
    expect(binaryResult.binaryLike, "NUL data must be binary-like when not Unicode text");

    const std::array<std::byte, 3> legacy{
        std::byte{0xE7}, std::byte{0xFD}, std::byte{0xFE}};
    const auto legacyResult = nff::encoding::EncodingDetector::detect(legacy);
    expect(legacyResult.encoding == nff::encoding::Encoding::Unknown8Bit,
           "legacy 8-bit data remains undecided until a code page is selected");

    std::array<std::byte, 100> controlHeavy{};
    controlHeavy.fill(std::byte{'a'});
    controlHeavy[3] = std::byte{0x01};
    controlHeavy[30] = std::byte{0x02};
    controlHeavy[70] = std::byte{0x03};
    const auto controlResult = nff::encoding::EncodingDetector::detect(controlHeavy);
    expect(controlResult.encoding == nff::encoding::Encoding::Binary &&
               controlResult.binaryLike,
           "control-heavy NUL-free input remains binary-like on the fast path");
}

void testUnicodeCodecs() {
    constexpr std::string_view text = "FasaFiso – Türkçe: İĞüş ı 🚀\r\nnext\n";

    for (const auto encoding : {nff::encoding::Encoding::Utf8,
                                nff::encoding::Encoding::Utf16LE,
                                nff::encoding::Encoding::Utf16BE,
                                nff::encoding::Encoding::Utf32LE,
                                nff::encoding::Encoding::Utf32BE}) {
        const auto encoded = nff::encoding::TextCodec::encode(text, encoding, true);
        expect(static_cast<bool>(encoded), "Unicode encode");
        if (!encoded) {
            continue;
        }

        const auto decoded = nff::encoding::TextCodec::decode(encoded.bytes, encoding, true);
        expect(static_cast<bool>(decoded), "Unicode decode");
        if (decoded) {
            expect(decoded.text == text, "Unicode round trip");
        }
    }

    const std::array<std::byte, 2> truncatedUtf16{std::byte{0x41}, std::byte{0x00}};
    const auto valid = nff::encoding::TextCodec::decode(
        truncatedUtf16, nff::encoding::Encoding::Utf16LE, false);
    expect(static_cast<bool>(valid) && valid.text == "A", "valid UTF-16 unit");

    const std::array<std::byte, 1> invalidUtf16{std::byte{0x41}};
    const auto invalid = nff::encoding::TextCodec::decode(
        invalidUtf16, nff::encoding::Encoding::Utf16LE, false);
    expect(!invalid, "truncated UTF-16 rejected for full document decode");

    const auto noBomUtf8 = bytes("plain");
    const auto falseBom = nff::encoding::TextCodec::decode(
        noBomUtf8, nff::encoding::Encoding::Utf8, true);
    expect(!falseBom, "declared BOM must be present and valid");

    const std::array<std::byte, 3> truncatedUtf8{
        std::byte{'A'}, std::byte{0xE2}, std::byte{0x82}};
    const auto rejectedTail = nff::encoding::TextCodec::decode(
        truncatedUtf8, nff::encoding::Encoding::Utf8, false);
    expect(!rejectedTail, "truncated UTF-8 is rejected for a full document");
    nff::encoding::DecodeOptions sampleOptions;
    sampleOptions.allowTruncatedTail = true;
    const auto acceptedTail = nff::encoding::TextCodec::decode(
        truncatedUtf8, nff::encoding::Encoding::Utf8, false, sampleOptions);
    expect(static_cast<bool>(acceptedTail) && acceptedTail.text == "A",
           "truncated UTF-8 sample keeps only the complete prefix");

    const std::array<std::byte, 2> nonAsciiBytes{std::byte{'A'}, std::byte{0x80}};
    const auto invalidAscii = nff::encoding::TextCodec::decode(
        nonAsciiBytes, nff::encoding::Encoding::Ascii, false);
    expect(!invalidAscii, "ASCII fast decode rejects high-bit bytes");
}

void testLegacyCodec() {
    constexpr std::string_view turkish = "Çağrı ŞİĞüş ı";
    const auto encoded = nff::encoding::TextCodec::encode(
        turkish, nff::encoding::Encoding::Windows1254);
    expect(static_cast<bool>(encoded), "Windows-1254 encode");
    if (!encoded) {
        return;
    }

    const auto decoded = nff::encoding::TextCodec::decode(
        encoded.bytes, nff::encoding::Encoding::Windows1254);
    expect(static_cast<bool>(decoded), "Windows-1254 decode");
    if (decoded) {
        expect(decoded.text == turkish, "Windows-1254 round trip");
    }
}

void testTextAnalysis() {
    const auto crlf = nff::core::TextAnalysis::analyzeUtf8("one\r\ntwo\r\n");
    expect(crlf.lineEnding == nff::core::LineEnding::CRLF, "CRLF analysis");
    expect(crlf.longestLineBytes == 3, "longest line analysis");

    const auto mixed = nff::core::TextAnalysis::analyzeUtf8("a\r\nb\nc\r");
    expect(mixed.lineEnding == nff::core::LineEnding::Mixed, "mixed line-ending analysis");
}

void testIncrementalLineProfileMatchesFullAnalysis() {
    struct Edit { std::size_t offset; std::size_t eraseBytes; std::string inserted; };
    const auto verify = [](const nff::core::Document& document, const std::string_view label) {
        const auto full = nff::core::TextAnalysis::analyzeUtf8(document.text());
        expect(document.profile().lineEnding == full.lineEnding,
               std::string(label) + " line ending matches full analysis");
        expect(document.profile().longestSampledLine == full.longestLineBytes,
               std::string(label) + " longest line matches full analysis");
    };

    nff::core::Document document;
    document.replaceText("alpha\r\nbeta-beta\ngamma\rdelta-delta\r\nomega");
    verify(document, "initial mixed profile");

    const std::vector<Edit> edits{
        {5U, 2U, "\n"},
        {10U, 0U, "\r\n"},
        {0U, 0U, "x\r"},
        {2U, 1U, "\r\n"},
        {document.text().size(), 0U, "\nlongest-longest-line"},
    };
    for (std::size_t index = 0U; index < edits.size(); ++index) {
        const auto& edit = edits[index];
        const auto safeOffset = std::min(edit.offset, document.text().size());
        const auto safeErase = std::min(edit.eraseBytes, document.text().size() - safeOffset);
        const auto error = document.applyEdit(safeOffset, safeErase, edit.inserted);
        expect(!error, "incremental line-profile edit accepted");
        verify(document, "incremental edit " + std::to_string(index));
    }

    document.replaceText("same-longest\none\nsame-longest\ntwo\n");
    verify(document, "equal longest baseline");
    const auto firstNewline = document.text().find('\n');
    const auto error = document.applyEdit(0U, firstNewline, "x");
    expect(!error, "removing one equal longest line accepted");
    verify(document, "equal-longest occurrence removal");

    document.replaceText("a\r\nbb\nccc\rdddd\n");
    constexpr std::array<std::string_view, 6> inserts{"", "\n", "\r", "\r\n", "x", "x\nx"};
    std::uint64_t random = 0x9E3779B97F4A7C15ULL;
    for (std::size_t iteration = 0U; iteration < 1500U; ++iteration) {
        random = random * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto size = document.text().size();
        const auto offset = size == 0U ? 0U : static_cast<std::size_t>(random % (size + 1U));
        random = random * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto maximumErase = std::min<std::size_t>(3U, size - offset);
        const auto erase = maximumErase == 0U ? 0U : static_cast<std::size_t>(random % (maximumErase + 1U));
        random = random * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto inserted = inserts[static_cast<std::size_t>(random % inserts.size())];
        const auto editError = document.applyEdit(offset, erase, inserted);
        expect(!editError, "random incremental line-profile edit accepted");
        verify(document, "random incremental profile");
        if (document.text().size() > 256U) document.replaceText("a\r\nbb\nccc\rdddd\n");
    }
}

void testUtf16InspectionAndPreservingSave() {
    const auto root = freshTempDirectory("nff-utf16-tests");
    const auto path = root / "utf16.txt";
    constexpr std::string_view original = "one\r\ntwo\r\n";

    const auto encoded = nff::encoding::TextCodec::encode(
        original, nff::encoding::Encoding::Utf16LE, true);
    expect(static_cast<bool>(encoded), "UTF-16 test encode");
    if (!encoded) {
        return;
    }

    auto error = nff::storage::FileWriter::writeAtomically(path, encoded.bytes);
    expect(!error, "write UTF-16 test document");

    const auto inspect = nff::core::FileSniffer::inspect(path);
    expect(static_cast<bool>(inspect), "inspect UTF-16 document");
    if (inspect) {
        expect(inspect.profile.encoding.encoding == nff::encoding::Encoding::Utf16LE,
               "UTF-16 profile encoding");
        expect(inspect.profile.encoding.hasBom, "UTF-16 profile BOM");
        expect(inspect.profile.lineEnding == nff::core::LineEnding::CRLF,
               "UTF-16 CRLF detection after decoding");
    }

    nff::core::Document document;
    error = document.load(path);
    expect(!error, "load UTF-16 document");
    expect(document.text() == original, "Document exposes canonical UTF-8 text");
    expect(document.saveEncoding() == nff::encoding::Encoding::Utf16LE,
           "Document preserves UTF-16 save encoding");
    expect(document.writesBom(), "Document preserves UTF-16 BOM");

    const std::string replacement = "güncel 🚀\r\ntext\r\n";
    document.replaceText(replacement);
    error = document.save();
    expect(!error, "save UTF-16 document");

    const auto readBack = nff::storage::FileReader::readAll(path, 4096);
    expect(static_cast<bool>(readBack), "read saved UTF-16 document");
    if (readBack) {
        const auto detection = nff::encoding::EncodingDetector::detect(readBack.bytes);
        expect(detection.encoding == nff::encoding::Encoding::Utf16LE,
               "saved document remains UTF-16 LE");
        expect(detection.hasBom, "saved document retains BOM");
        const auto decoded = nff::encoding::TextCodec::decode(readBack.bytes, detection);
        expect(static_cast<bool>(decoded) && decoded.text == replacement,
               "saved UTF-16 content round trip");
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testAsciiUpgradeAndAtomicWrite() {
    const auto root = freshTempDirectory("nff-ascii-tests");
    const auto path = root / "sample.txt";
    auto initial = bytes("one\ntwo\n");
    auto error = nff::storage::FileWriter::writeAtomically(path, initial);
    expect(!error, "atomic ASCII initial write");

    nff::core::Document document;
    error = document.load(path);
    expect(!error, "ASCII document load");
    expect(document.saveEncoding() == nff::encoding::Encoding::Utf8,
           "ASCII documents use UTF-8 as safe save encoding");

    const std::string replacement = "Çağrı\n";
    document.replaceText(replacement);
    expect(document.modified(), "replacement marks document modified");
    error = document.save();
    expect(!error, "ASCII document upgrades to UTF-8 on save");
    expect(!document.modified(), "saved document is clean");

    const auto readBack = nff::storage::FileReader::readAll(path, 1024);
    expect(static_cast<bool>(readBack), "read back upgraded file");
    if (readBack) {
        const auto detection = nff::encoding::EncodingDetector::detect(readBack.bytes);
        expect(detection.encoding == nff::encoding::Encoding::Utf8,
               "non-ASCII replacement saved as UTF-8");
        expect(!detection.hasBom, "ASCII upgrade does not invent a BOM");
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testExplicitLegacyLoad() {
    const auto root = freshTempDirectory("nff-legacy-load-tests");
    const auto path = root / "legacy.txt";
    constexpr std::string_view text = "Türkçe: ğüşİı";

    const auto encoded = nff::encoding::TextCodec::encode(
        text, nff::encoding::Encoding::Windows1254);
    expect(static_cast<bool>(encoded), "legacy load fixture encode");
    if (!encoded) {
        return;
    }
    auto error = nff::storage::FileWriter::writeAtomically(path, encoded.bytes);
    expect(!error, "legacy load fixture write");

    nff::core::Document autoDocument;
    error = autoDocument.load(path);
    expect(error == std::make_error_code(std::errc::operation_not_supported),
           "unknown legacy encoding is not guessed destructively");

    nff::core::Document explicitDocument;
    error = explicitDocument.loadAs(path, nff::encoding::Encoding::Windows1254);
    expect(!error, "explicit Windows-1254 load");
    expect(explicitDocument.text() == text, "explicit legacy load decodes correctly");
    expect(explicitDocument.saveEncoding() == nff::encoding::Encoding::Windows1254,
           "legacy encoding is preserved for save");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testExplicitOutputRepresentationPolicy() {
    const auto root = freshTempDirectory("nff-output-policy-tests");
    const auto path = root / "converted.data";

    nff::core::Document document;
    document.replaceText("Turkce ascii\n");
    document.markClean();
    const auto revision = document.revision();

    auto error = document.setSaveEncoding(nff::encoding::Encoding::Utf16LE);
    expect(!error, "set UTF-16 output encoding");
    expect(document.saveEncoding() == nff::encoding::Encoding::Utf16LE,
           "output encoding policy updates without rewriting text");
    expect(document.modified(), "encoding policy change marks document modified");
    expect(document.revision() == revision,
           "encoding policy change does not invalidate editor text revision");

    error = document.setWritesBom(true);
    expect(!error && document.writesBom(), "Unicode output policy accepts BOM");
    expect(document.revision() == revision,
           "BOM policy change does not invalidate editor text revision");

    error = document.saveAs(path);
    expect(!error, "normal Save As commits selected output policy");
    const auto readBack = nff::storage::FileReader::readAll(path, 4096U);
    expect(static_cast<bool>(readBack), "read converted output policy file");
    if (readBack) {
        const auto detected = nff::encoding::EncodingDetector::detect(readBack.bytes);
        expect(detected.encoding == nff::encoding::Encoding::Utf16LE && detected.hasBom,
               "selected encoding and BOM are written without special save options");
    }

    error = document.setSaveEncoding(nff::encoding::Encoding::Windows1254);
    expect(!error, "set Windows-1254 output encoding");
    expect(!document.writesBom(), "legacy encoding automatically drops incompatible BOM");
    error = document.setWritesBom(true);
    expect(error == std::make_error_code(std::errc::invalid_argument),
           "legacy encoding rejects BOM policy");

    error = document.setSaveEncoding(nff::encoding::Encoding::Binary);
    expect(error == std::make_error_code(std::errc::invalid_argument),
           "binary cannot become a text save encoding");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testLargeFileInspectionCapsDefaultSampleWork() {
    const auto root = freshTempDirectory("nff-large-inspection-sample-cap");
    const auto path = root / "huge.txt";

    constexpr std::size_t prefixBytes = 4U * 1024U * 1024U;
    std::string prefix(prefixBytes, '7');
    auto error = nff::storage::FileWriter::writeAtomically(path, bytes(prefix));
    expect(!error, "write large inspection prefix");
    if (error) {
        return;
    }

    std::filesystem::resize_file(path, 128ULL * 1024ULL * 1024ULL, error);
    expect(!error, "extend large inspection fixture");
    if (error) {
        return;
    }

    nff::core::InspectOptions options;
    const auto inspect = nff::core::FileSniffer::inspect(path, options);
    expect(static_cast<bool>(inspect), "inspect already-large file");
    if (inspect) {
        expect(inspect.profile.recommendedMode == nff::core::OpenMode::Viewer,
               "already-large file remains routed to viewer");
        expect(inspect.profile.longestSampledLine == options.longLineThreshold,
               "already-large file caps default inspection sample at long-line threshold");
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testManagerExplicitEncodingReload() {
    const auto root = freshTempDirectory("nff-manager-reopen-encoding-tests");
    const auto path = root / "legacy-turkish.txt";
    constexpr std::string_view expected = "Türkçe: ğüşİı";

    const auto encoded = nff::encoding::TextCodec::encode(
        expected, nff::encoding::Encoding::Windows1254);
    expect(static_cast<bool>(encoded), "manager legacy fixture encode");
    if (!encoded) {
        return;
    }
    auto error = nff::storage::FileWriter::writeAtomically(path, encoded.bytes);
    expect(!error, "manager legacy fixture write");

    const auto before = nff::storage::FileReader::readAll(path, 4096U);
    expect(static_cast<bool>(before), "manager legacy fixture read before reopen");

    nff::core::DocumentManager manager;
    const auto opened = manager.open(path);
    expect(static_cast<bool>(opened), "manager opens undecidable legacy file as a safe reference");
    if (!opened) {
        std::filesystem::remove_all(root, error);
        return;
    }

    auto* document = manager.get(opened.id);
    expect(document != nullptr && !document->textBufferLoaded(),
           "undecidable legacy file starts without a decoded text buffer");
    expect(document != nullptr &&
               document->profile().recommendedMode == nff::core::OpenMode::BinaryPreview,
           "undecidable legacy file starts in safe Hex Preview");

    error = manager.reloadAsEncoding(opened.id, nff::encoding::Encoding::Windows1254);
    expect(!error, "manager explicitly reopens legacy bytes as Windows-1254");
    document = manager.get(opened.id);
    expect(document != nullptr && document->textBufferLoaded(),
           "explicit reopen materializes canonical text");
    expect(document != nullptr && document->text() == expected,
           "explicit reopen decodes Turkish text correctly");
    expect(document != nullptr &&
               document->profile().recommendedMode == nff::core::OpenMode::Editor,
           "explicit reopen promotes the same document to Editor mode");
    expect(document != nullptr &&
               document->saveEncoding() == nff::encoding::Encoding::Windows1254,
           "explicit source encoding becomes the preserved save encoding");

    const auto after = nff::storage::FileReader::readAll(path, 4096U);
    expect(static_cast<bool>(after) && static_cast<bool>(before) && after.bytes == before.bytes,
           "reopen-as never writes or mutates the file on disk");

    const auto stableText = document != nullptr ? std::string(document->text()) : std::string{};
    error = manager.reloadAsEncoding(opened.id, nff::encoding::Encoding::Utf8);
    expect(static_cast<bool>(error), "wrong explicit UTF-8 selection fails safely");
    document = manager.get(opened.id);
    expect(document != nullptr && document->text() == stableText &&
               document->saveEncoding() == nff::encoding::Encoding::Windows1254,
           "failed explicit reopen leaves the live document unchanged");

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testManagerExplicitEncodingReloadStaysScalable() {
    const auto root = freshTempDirectory("nff-manager-reopen-encoding-scalable-tests");
    const auto path = root / "legacy-turkish-large-route.txt";
    constexpr std::string_view expected = "Türkçe: ğüşİı\n";

    const auto encoded = nff::encoding::TextCodec::encode(
        expected, nff::encoding::Encoding::Windows1254);
    expect(static_cast<bool>(encoded), "scalable legacy fixture encode");
    if (!encoded) {
        return;
    }
    auto error = nff::storage::FileWriter::writeAtomically(path, encoded.bytes);
    expect(!error, "scalable legacy fixture write");

    nff::core::DocumentManager manager;
    const auto opened = manager.open(path);
    expect(static_cast<bool>(opened), "scalable legacy fixture opens as safe reference");
    if (!opened) {
        std::error_code cleanupError;
        std::filesystem::remove_all(root, cleanupError);
        return;
    }

    error = manager.reloadAsEncoding(opened.id, nff::encoding::Encoding::Windows1254, 4U);
    expect(!error, "explicit source encoding routes oversized text without materializing it");

    auto* document = manager.get(opened.id);
    expect(document != nullptr && !document->textBufferLoaded(),
           "oversized explicit reopen stays out of the editor buffer");
    expect(document != nullptr &&
               document->profile().recommendedMode == nff::core::OpenMode::Viewer,
           "oversized explicit reopen promotes the same document to scalable Viewer mode");
    expect(document != nullptr &&
               document->profile().encoding.encoding == nff::encoding::Encoding::Windows1254 &&
               !document->profile().encoding.binaryLike,
           "oversized explicit reopen records the selected source encoding safely");
    expect(document != nullptr &&
               document->saveEncoding() == nff::encoding::Encoding::Windows1254,
           "scalable explicit source encoding is retained as document representation metadata");

    if (document != nullptr && document->trackedFileState() != nullptr) {
        nff::viewer::LargeFileViewer viewer;
        const auto viewerOpened = viewer.openPrepared(
            path, document->profile(), *document->trackedFileState());
        expect(static_cast<bool>(viewerOpened),
               "prepared scalable viewer accepts the explicitly selected legacy encoding");
        if (viewerOpened) {
            const auto window = viewer.readTextWindow(0U, 4096U);
            expect(static_cast<bool>(window) && window.text == expected,
                   "scalable viewer decodes Windows-1254 lazily with no full-file editor load");
        }
    } else {
        expect(false, "scalable explicit reopen keeps a tracked file snapshot");
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

void testViewerRecommendation() {
    const auto root = freshTempDirectory("nff-viewer-threshold-test");
    const auto path = root / "long-line.txt";

    auto content = bytes("0123456789\n");
    const auto error = nff::storage::FileWriter::writeAtomically(path, content);
    expect(!error, "viewer test write");

    nff::core::InspectOptions options;
    options.longLineThreshold = 8;
    options.viewerSizeThreshold = 1024;
    options.sampleBytes = 1024;

    const auto inspect = nff::core::FileSniffer::inspect(path, options);
    expect(static_cast<bool>(inspect), "viewer test inspect");
    if (inspect) {
        expect(inspect.profile.recommendedMode == nff::core::OpenMode::Viewer,
               "long line should recommend viewer");
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}

}

void testUtf8FastPathBoundaries() {
    const std::array<std::string, 5> valid{ "a", "\xc2\x80", "\xe0\xa0\x80", "\xf0\x90\x80\x80", "\xf4\x8f\xbf\xbf" };
    const std::array<std::string, 10> invalid{ "\x80", "\xc0\x80", "\xc1\xbf", "\xe0\x80\x80", "\xed\xa0\x80", "\xf0\x80\x80\x80", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xc2", "\xe2\x82" };
    for (std::size_t prefix = 0; prefix < 32; ++prefix) {
        for (const auto& sequence : valid) {
            nff::core::Document document;
            const auto text = std::string(prefix, 'x') + sequence + std::string(33, 'y');
            expect(!document.applyEdit(0, 0, text), "valid UTF-8 at word boundaries");
            expect(document.text() == text, "valid UTF-8 preserved");
        }
        for (const auto& sequence : invalid) {
            nff::core::Document document;
            document.replaceText("unchanged");
            const auto revision = document.revision();
            const auto text = std::string(prefix, 'x') + sequence;
            expect(document.applyEdit(0, 0, text) == std::errc::illegal_byte_sequence, "invalid UTF-8 at word boundaries");
            expect(document.text() == "unchanged" && document.revision() == revision, "rejected UTF-8 edit is transactional");
        }
    }
}

int main() {
    testUtf8FastPathBoundaries();
    testEncodingDetection();
    testUnicodeCodecs();
    testLegacyCodec();
    testTextAnalysis();
    testIncrementalLineProfileMatchesFullAnalysis();
    testUtf16InspectionAndPreservingSave();
    testAsciiUpgradeAndAtomicWrite();
    testExplicitLegacyLoad();
    testExplicitOutputRepresentationPolicy();
    testLargeFileInspectionCapsDefaultSampleWork();
    testManagerExplicitEncodingReload();
    testManagerExplicitEncodingReloadStaysScalable();
    testViewerRecommendation();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "all tests passed\n";
    return 0;
}
