#include "notepadFasaFiso/core/FileSniffer.hpp"
#include "notepadFasaFiso/encoding/TextCodec.hpp"
#include "notepadFasaFiso/formats/FormatDetector.hpp"
#include "notepadFasaFiso/storage/FileWriter.hpp"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using nff::formats::TextFormat;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void testExtensionAndSpecialNames() {
    const auto cpp = nff::formats::FormatDetector::detect("hello.hpp", "not enough content");
    require(cpp.format == TextFormat::Cpp && cpp.matchedExtension, "C++ extension detection");

    const auto make = nff::formats::FormatDetector::detect("Makefile", "all:\n\techo ok\n");
    require(make.format == TextFormat::Makefile, "Makefile name detection");

    const auto docker = nff::formats::FormatDetector::detect(
        "Dockerfile.dev", "FROM alpine\nRUN echo ok\n");
    require(docker.format == TextFormat::Dockerfile, "Dockerfile name detection");

    const auto cmake = nff::formats::FormatDetector::detect(
        "CMakeLists.txt", "cmake_minimum_required(VERSION 3.24)\nproject(test)\n");
    require(cmake.format == TextFormat::CMake, "CMakeLists special-name detection");
}

void testStrongContentOverridesWeakExtension() {
    constexpr std::string_view json = R"({"name":"FasaFiso","enabled":true,"items":[1,2,3]})";
    const auto result = nff::formats::FormatDetector::detect("notes.txt", json);
    require(result.format == TextFormat::Json, "strong JSON content overrides .txt");
    require(result.matchedContent, "JSON content match flag");
    require(result.confidence == 100U, "JSON content confidence");

    const auto invalidJson = nff::formats::FormatDetector::detect("config.json", "{ broken");
    require(invalidJson.format == TextFormat::Json && invalidJson.matchedExtension,
            "extension remains useful for temporarily invalid structured text");
}

void testExtensionDisambiguatesKeyValueFormats() {
    constexpr std::string_view values = "APP_PORT=8080\nAPP_HOST=localhost\nAPP_MODE=dev\n";
    const auto env = nff::formats::FormatDetector::detect(".env", values);
    require(env.format == TextFormat::Env && env.matchedContent,
            ".env extension disambiguates generic key/value syntax");

    const auto properties = nff::formats::FormatDetector::detect("app.properties", values);
    require(properties.format == TextFormat::Properties,
            "properties extension disambiguates generic key/value syntax");

    const auto ini = nff::formats::FormatDetector::detect(
        "app.ini", "[server]\nhost=localhost\nport=8080\n");
    require(ini.format == TextFormat::Ini, "INI extension wins inside key/value family");
}

void testStructuredContent() {
    const auto jsonl = nff::formats::FormatDetector::detect(
        "events", "{\"id\":1}\n{\"id\":2}\n{\"id\":3}\n");
    require(jsonl.format == TextFormat::JsonLines, "JSON Lines content detection");

    const auto html = nff::formats::FormatDetector::detect(
        "page", "<!doctype html>\n<html><body>Hello</body></html>\n");
    require(html.format == TextFormat::Html, "HTML content detection");

    const auto xml = nff::formats::FormatDetector::detect(
        "document", "<?xml version=\"1.0\"?><root><item>1</item></root>");
    require(xml.format == TextFormat::Xml, "XML content detection");

    const auto yaml = nff::formats::FormatDetector::detect(
        "config", "---\nserver:\n  host: localhost\n  port: 8080\n");
    require(yaml.format == TextFormat::Yaml, "YAML heuristic detection");
}

void testDelimitedContent() {
    const auto csv = nff::formats::FormatDetector::detect(
        "data", "name,age,city\nAda,25,London\nLinus,30,Helsinki\n");
    require(csv.format == TextFormat::Csv, "CSV content detection");

    const auto quotedCsv = nff::formats::FormatDetector::detect(
        "quoted", "name,note\nAda,\"hello, world\"\nLinus,\"kernel\"\n");
    require(quotedCsv.format == TextFormat::Csv, "quoted CSV delimiter handling");

    const auto tsv = nff::formats::FormatDetector::detect(
        "table", "name\tage\tcity\nAda\t25\tLondon\nLinus\t30\tHelsinki\n");
    require(tsv.format == TextFormat::Tsv, "TSV content detection");
}

void testHumanTextAndLogs() {
    const auto markdown = nff::formats::FormatDetector::detect(
        "notes", "# Header\n\n- [ ] task\n\n[link](https://example.com)\n");
    require(markdown.format == TextFormat::Markdown, "Markdown content detection");

    const auto log = nff::formats::FormatDetector::detect(
        "server", "2026-08-18 10:00:00 INFO boot\n2026-08-18 10:00:01 WARN warm\n"
                  "2026-08-18 10:00:02 ERROR failed\n");
    require(log.format == TextFormat::Log, "log content detection");

    const auto plain = nff::formats::FormatDetector::detect(
        "untitled", "just a normal sentence\nand another normal sentence\n");
    require(plain.format == TextFormat::PlainText, "ordinary prose remains plain text");
}

void testShebangAndPatchDetection() {
    const auto python = nff::formats::FormatDetector::detect(
        "tool", "#!/usr/bin/env python3\nprint('hello')\n");
    require(python.format == TextFormat::Python, "Python shebang detection");

    const auto shell = nff::formats::FormatDetector::detect(
        "tool", "#!/bin/bash\necho hello\n");
    require(shell.format == TextFormat::Shell, "shell shebang detection");

    const auto diff = nff::formats::FormatDetector::detect(
        "change", "diff --git a/a.txt b/a.txt\n--- a/a.txt\n+++ b/a.txt\n@@ -1 +1 @@\n-old\n+new\n");
    require(diff.format == TextFormat::Diff, "diff content detection");
}

void testDescriptorCapabilities() {
    const auto& json = nff::formats::FormatDetector::descriptor(TextFormat::Json);
    require(json.name == "JSON", "JSON descriptor name");
    require(json.capabilities.validation, "JSON validation capability");
    require(json.capabilities.prettyPrint, "JSON pretty-print capability");

    const auto& csv = nff::formats::FormatDetector::descriptor(TextFormat::Csv);
    require(csv.capabilities.tableView, "CSV table-view capability");

    const auto& markdown = nff::formats::FormatDetector::descriptor(TextFormat::Markdown);
    require(markdown.capabilities.markupPreview, "Markdown preview capability");

    require(nff::formats::FormatDetector::descriptors().size() == 33U,
            "descriptor registry remains complete");
}

void testInspectorUsesDecodedText() {
    const auto root = std::filesystem::temp_directory_path() / "nff-format-inspector";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    require(!error, "create format inspector directory");

    const auto path = root / "mystery.data";
    constexpr std::string_view json = "{\"turkish\":\"İstanbul\",\"ok\":true}\r\n";
    const auto encoded = nff::encoding::TextCodec::encode(
        json, nff::encoding::Encoding::Utf16LE, true);
    require(static_cast<bool>(encoded), "encode UTF-16 format fixture");
    error = nff::storage::FileWriter::writeAtomically(path, encoded.bytes);
    require(!error, "write UTF-16 format fixture");

    const auto inspected = nff::core::FileSniffer::inspect(path);
    require(static_cast<bool>(inspected), "inspect format fixture");
    require(inspected.profile.format.format == TextFormat::Json,
            "inspector detects format after Unicode decode");
    require(inspected.profile.encoding.encoding == nff::encoding::Encoding::Utf16LE,
            "format inspection preserves encoding detection");

    std::filesystem::remove_all(root, error);
}

}

int main() {
    testExtensionAndSpecialNames();
    testStrongContentOverridesWeakExtension();
    testExtensionDisambiguatesKeyValueFormats();
    testStructuredContent();
    testDelimitedContent();
    testHumanTextAndLogs();
    testShebangAndPatchDetection();
    testDescriptorCapabilities();
    testInspectorUsesDecodedText();
    return EXIT_SUCCESS;
}
