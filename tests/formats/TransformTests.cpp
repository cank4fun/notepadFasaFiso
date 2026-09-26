#include "notepadFasaFiso/core/ExportService.hpp"
#include "notepadFasaFiso/encoding/TextCodec.hpp"
#include "notepadFasaFiso/formats/FormatTransform.hpp"
#include "notepadFasaFiso/storage/FileReader.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void testJsonValidationAndFormatting() {
    constexpr std::string_view input =
        R"({"name":"Fasa\\nFiso","items":[1,true,null,{"x":2.5e+3}],"empty":{}})";

    const auto validation = nff::formats::FormatTransform::validateJson(input);
    require(validation.valid, "valid JSON rejected");

    const auto pretty = nff::formats::FormatTransform::prettyJson(
        input, nff::formats::JsonFormatOptions{4U, {}});
    require(static_cast<bool>(pretty), "JSON pretty print failed");
    require(pretty.text.find("\n    \"name\":") != std::string::npos,
            "JSON pretty indentation missing");

    const auto minified = nff::formats::FormatTransform::minifyJson(pretty.text);
    require(static_cast<bool>(minified), "JSON minify failed");
    require(minified.text == input, "JSON pretty/minify roundtrip changed tokens");
}

void testJsonFailureLocationAndLimits() {
    constexpr std::string_view invalid = "{\n  \"x\": [1, 2,]\n}";
    const auto validation = nff::formats::FormatTransform::validateJson(invalid);
    require(!validation.valid, "invalid JSON accepted");
    require(validation.line == 2U, "JSON error line is incorrect");
    require(validation.column > 1U, "JSON error column is incorrect");

    nff::formats::TransformLimits limits;
    limits.maxNesting = 2U;
    const auto nested = nff::formats::FormatTransform::minifyJson("[[[0]]]", limits);
    require(!nested, "JSON nesting limit ignored");

    limits = {};
    limits.maxOutputBytes = 4U;
    const auto limited = nff::formats::FormatTransform::prettyJson("{\"a\":1}", {2U, limits});
    require(!limited, "JSON output limit ignored");
    require(limited.error == nff::formats::make_error_code(
                                 nff::formats::FormatTransformErrc::OutputLimitExceeded),
            "wrong JSON output-limit error");
}

void testDelimitedConversion() {
    constexpr std::string_view csv =
        "name,note,value\r\n"
        "alpha,\"hello, world\",1\r\n"
        "beta,\"line1\nline2\",\"a\"\"b\"\r\n";

    const auto tsv = nff::formats::FormatTransform::convertDelimited(
        csv, {',', '\t', {}});
    require(static_cast<bool>(tsv), "CSV to TSV failed");
    require(tsv.text.starts_with("name\tnote\tvalue\n"), "CSV header conversion failed");
    require(tsv.text.find("\"hello, world\"") == std::string::npos,
            "unneeded CSV quotes were retained");
    require(tsv.text.find("\"line1\nline2\"") != std::string::npos,
            "embedded newline lost quoting");

    const auto csvAgain = nff::formats::FormatTransform::convertDelimited(
        tsv.text, {'\t', ',', {}});
    require(static_cast<bool>(csvAgain), "TSV to CSV failed");
    require(csvAgain.text.find("\"hello, world\"") != std::string::npos,
            "CSV comma field not requoted");
    require(csvAgain.text.ends_with('\n'), "trailing record terminator not preserved");
}

void testDelimitedValidation() {
    const auto badQuote = nff::formats::FormatTransform::convertDelimited(
        "a,\"unterminated\n", {',', '\t', {}});
    require(!badQuote, "unterminated CSV quote accepted");

    const auto badAfterQuote = nff::formats::FormatTransform::convertDelimited(
        "a,\"b\"oops,c", {',', '\t', {}});
    require(!badAfterQuote, "garbage after CSV closing quote accepted");
}

void testExportRenderAndEncoding() {
    nff::core::ExportOptions options;
    options.transform = nff::core::ContentTransform::JsonPretty;
    options.jsonIndentWidth = 2U;
    options.save.lineEnding = nff::core::LineEndingPolicy::CRLF;
    options.save.encoding = nff::encoding::Encoding::Utf16LE;
    options.save.writeBom = true;

    const auto rendered = nff::core::ExportService::render("{\"x\":[1,2]}", options);
    require(static_cast<bool>(rendered), "export render failed");
    require(rendered.text.find("\r\n") != std::string::npos,
            "export line-ending conversion failed");
    require(rendered.text.find("\n") != std::string::npos, "formatted JSON unexpectedly single-line");

    const auto path = std::filesystem::temp_directory_path() / "nff-transform-export-test.json";
    const auto written = nff::core::ExportService::write(path, "{\"x\":[1,2]}", options);
    require(static_cast<bool>(written), "export write failed");

    const auto bytes = nff::storage::FileReader::readAll(path, 1024U * 1024U);
    require(static_cast<bool>(bytes), "failed to read exported file");
    require(bytes.bytes.size() >= 2U, "exported UTF-16 file too small");
    require(bytes.bytes[0] == std::byte{0xFF} && bytes.bytes[1] == std::byte{0xFE},
            "UTF-16LE BOM missing");

    const auto decoded = nff::encoding::TextCodec::decode(
        bytes.bytes, nff::encoding::Encoding::Utf16LE, true);
    require(static_cast<bool>(decoded), "exported UTF-16 file failed to decode");
    require(decoded.text == rendered.text, "written export differs from rendered export");

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void testNoTransformExport() {
    nff::core::ExportOptions options;
    options.save.lineEnding = nff::core::LineEndingPolicy::LF;
    const auto rendered = nff::core::ExportService::render("a\r\nb\rc\n", options);
    require(static_cast<bool>(rendered), "plain export render failed");
    require(rendered.text == "a\nb\nc\n", "plain export newline conversion failed");
}

}

int main() {
    testJsonValidationAndFormatting();
    testJsonFailureLocationAndLimits();
    testDelimitedConversion();
    testDelimitedValidation();
    testExportRenderAndEncoding();
    testNoTransformExport();
    return EXIT_SUCCESS;
}
