#include "notepadFasaFiso/core/TextUtilities.hpp"
#include "notepadFasaFiso/storage/FileWriter.hpp"
#include "notepadFasaFiso/viewer/HexPreview.hpp"

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

[[nodiscard]] std::filesystem::path tempRoot(const std::string_view name) {
    const auto root = std::filesystem::temp_directory_path() / std::string{name};
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    expect(!error, "create utility temp directory");
    return root;
}

void testWhitespaceUtilities() {
    using nff::core::TextUtilities;

    expect(TextUtilities::trimTrailingWhitespace("a  \r\nb\t\nc   ") == "a\r\nb\nc",
           "trim trailing whitespace preserves terminators");
    expect(TextUtilities::removeEmptyLines("a\r\n\r\n  \nb\r", true) == "a\r\nb\r",
           "remove whitespace-only lines");
    expect(TextUtilities::removeEmptyLines("a\n  \nb\n", false) == "a\n  \nb\n",
           "keep whitespace-only lines when requested");
}

void testLineUtilities() {
    using nff::core::DuplicateLineOptions;
    using nff::core::LineSortDirection;
    using nff::core::TextUtilities;

    expect(TextUtilities::sortLines("b\r\na\nc", LineSortDirection::Ascending) ==
               "a\r\nb\nc",
           "sort preserves positional mixed terminators");
    expect(TextUtilities::reverseLines("one\r\ntwo\nthree") == "three\r\ntwo\none",
           "reverse preserves final termination state");

    DuplicateLineOptions options;
    options.caseSensitive = false;
    expect(TextUtilities::removeDuplicateLines("Alpha\nalpha\nBeta\n", options) ==
               "Alpha\nBeta\n",
           "case-insensitive duplicate removal");
    expect(TextUtilities::removeDuplicateLines("a\n\na\n\n", options) ==
               "a\n\n\n",
           "duplicate removal preserves empty lines when requested");
}

void testIndentAndAsciiCase() {
    using nff::core::TextUtilities;

    expect(TextUtilities::expandTabs("\tA\n12\tB", 4U) == "    A\n12  B",
           "tab expansion follows tab stops");
    expect(TextUtilities::compressLeadingSpacesToTabs("        x\n   y\n", 4U) ==
               "\t\tx\n   y\n",
           "only complete leading tab stops are compressed");
    expect(TextUtilities::asciiToLower("ABC İĞ") == "abc İĞ",
           "ASCII lower leaves UTF-8 bytes untouched");
    expect(TextUtilities::asciiToUpper("abc ıü") == "ABC ıü",
           "ASCII upper leaves UTF-8 bytes untouched");
}

void testHexPreview() {
    const auto root = tempRoot("nff-hex-preview");
    const auto path = root / "blob.bin";

    std::vector<std::byte> payload(100U);
    for (std::size_t index = 0U; index < payload.size(); ++index) {
        payload[index] = static_cast<std::byte>(index);
    }
    auto error = nff::storage::FileWriter::writeAtomically(path, payload);
    expect(!error, "write hex preview fixture");

    nff::viewer::HexPreview preview;
    error = preview.open(path);
    expect(!error && preview.isOpen(), "open hex preview");
    expect(preview.size() == payload.size(), "hex preview file size");
    expect(preview.rowCount(16U) == 7U, "hex row count rounds up");

    const auto window = preview.readRows(2U, 3U, 16U, 64U);
    expect(static_cast<bool>(window), "read hex rows");
    expect(window.startOffset == 32U && window.rowCount == 3U,
           "hex window coordinates");
    expect(window.bytes.size() == 48U, "hex window byte count");
    expect(window.row(0U).size() == 16U && window.row(2U).size() == 16U,
           "hex row spans");
    if (!window.bytes.empty()) {
        expect(std::to_integer<unsigned int>(window.bytes.front()) == 32U,
               "hex random-access contents");
    }

    const auto tail = preview.readRows(6U, 10U, 16U, 1024U);
    expect(static_cast<bool>(tail) && tail.bytes.size() == 4U && tail.rowCount == 1U,
           "hex tail is clipped to file size");

    const auto capped = preview.readRows(0U, 1000U, 16U, 32U);
    expect(static_cast<bool>(capped) && capped.bytes.size() == 32U && capped.rowCount == 2U,
           "hex preview honors memory budget");

    expect(nff::viewer::HexPreview::printableAscii(std::byte{'A'}) == 'A',
           "printable ASCII passthrough");
    expect(nff::viewer::HexPreview::printableAscii(std::byte{0x00}) == '.',
           "nonprintable ASCII placeholder");

    std::filesystem::remove_all(root, error);
}

}

int main() {
    testWhitespaceUtilities();
    testLineUtilities();
    testIndentAndAsciiCase();
    testHexPreview();

    if (failures != 0) {
        std::cerr << failures << " utility test(s) failed\n";
        return 1;
    }
    return 0;
}
