#include "notepadFasaFiso/formats/FormatDetector.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace nff::formats {
namespace {

using TF = TextFormat;

constexpr FormatCapabilities syntax{true, false, false, false, false, false, false, false};
constexpr FormatCapabilities structured{true, true, true, false, false, false, false, false};
constexpr FormatCapabilities jsonCaps{true, true, true, true, true, false, false, false};
constexpr FormatCapabilities tableCaps{true, true, false, false, false, true, false, true};
constexpr FormatCapabilities markdownCaps{true, false, false, false, false, false, true, false};
constexpr FormatCapabilities logCaps{true, false, false, false, false, false, false, true};

constexpr std::array<FormatDescriptor, 33> kDescriptors{{
    {TF::PlainText, "Plain Text", ".txt", {}},
    {TF::Log, "Log", ".log", logCaps},
    {TF::Markdown, "Markdown", ".md", markdownCaps},
    {TF::Json, "JSON", ".json", jsonCaps},
    {TF::JsonLines, "JSON Lines", ".jsonl", {true, true, true, false, false, false, false, true}},
    {TF::Xml, "XML", ".xml", {true, true, true, true, true, false, false, false}},
    {TF::Html, "HTML", ".html", {true, true, false, false, false, false, true, false}},
    {TF::Css, "CSS", ".css", syntax},
    {TF::Yaml, "YAML", ".yaml", structured},
    {TF::Toml, "TOML", ".toml", structured},
    {TF::Ini, "INI", ".ini", structured},
    {TF::Properties, "Properties", ".properties", structured},
    {TF::Env, "Environment", ".env", structured},
    {TF::Csv, "CSV", ".csv", tableCaps},
    {TF::Tsv, "TSV", ".tsv", tableCaps},
    {TF::JavaScript, "JavaScript", ".js", syntax},
    {TF::TypeScript, "TypeScript", ".ts", syntax},
    {TF::C, "C", ".c", syntax},
    {TF::Cpp, "C++", ".cpp", syntax},
    {TF::CSharp, "C#", ".cs", syntax},
    {TF::Java, "Java", ".java", syntax},
    {TF::Python, "Python", ".py", syntax},
    {TF::Rust, "Rust", ".rs", syntax},
    {TF::Go, "Go", ".go", syntax},
    {TF::Shell, "Shell", ".sh", syntax},
    {TF::Batch, "Batch", ".bat", syntax},
    {TF::PowerShell, "PowerShell", ".ps1", syntax},
    {TF::Lua, "Lua", ".lua", syntax},
    {TF::Sql, "SQL", ".sql", syntax},
    {TF::CMake, "CMake", ".cmake", syntax},
    {TF::Makefile, "Makefile", {}, syntax},
    {TF::Dockerfile, "Dockerfile", {}, syntax},
    {TF::Diff, "Diff/Patch", ".diff", syntax},
}};

struct ExtensionEntry {
    std::string_view extension;
    TextFormat format;
};

constexpr std::array<ExtensionEntry, 52> kExtensions{{
    {".txt", TF::PlainText}, {".text", TF::PlainText}, {".log", TF::Log},
    {".md", TF::Markdown}, {".markdown", TF::Markdown}, {".mdown", TF::Markdown},
    {".json", TF::Json}, {".jsonl", TF::JsonLines}, {".ndjson", TF::JsonLines},
    {".xml", TF::Xml}, {".xsd", TF::Xml}, {".svg", TF::Xml},
    {".html", TF::Html}, {".htm", TF::Html}, {".css", TF::Css},
    {".yaml", TF::Yaml}, {".yml", TF::Yaml}, {".toml", TF::Toml},
    {".ini", TF::Ini}, {".cfg", TF::Ini}, {".conf", TF::Ini},
    {".properties", TF::Properties}, {".env", TF::Env}, {".csv", TF::Csv},
    {".tsv", TF::Tsv}, {".js", TF::JavaScript}, {".mjs", TF::JavaScript},
    {".cjs", TF::JavaScript}, {".jsx", TF::JavaScript}, {".ts", TF::TypeScript},
    {".tsx", TF::TypeScript}, {".c", TF::C}, {".h", TF::C}, {".cpp", TF::Cpp},
    {".cc", TF::Cpp}, {".cxx", TF::Cpp}, {".hpp", TF::Cpp}, {".hh", TF::Cpp},
    {".hxx", TF::Cpp}, {".cs", TF::CSharp}, {".java", TF::Java}, {".py", TF::Python},
    {".pyw", TF::Python}, {".rs", TF::Rust}, {".go", TF::Go}, {".sh", TF::Shell},
    {".bash", TF::Shell}, {".zsh", TF::Shell}, {".bat", TF::Batch}, {".cmd", TF::Batch},
    {".ps1", TF::PowerShell}, {".lua", TF::Lua},
}};

[[nodiscard]] constexpr bool asciiSpace(const char ch) noexcept {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v';
}

[[nodiscard]] char asciiLower(const char value) noexcept {
    const auto byte = static_cast<unsigned char>(value);
    return static_cast<char>(std::tolower(byte));
}

[[nodiscard]] std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), asciiLower);
    return value;
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && asciiSpace(text.front())) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && asciiSpace(text.back())) {
        text.remove_suffix(1U);
    }
    return text;
}

[[nodiscard]] bool startsWithInsensitive(const std::string_view value,
                                         const std::string_view prefix) noexcept {
    if (value.size() < prefix.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < prefix.size(); ++index) {
        if (asciiLower(value[index]) != asciiLower(prefix[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool containsInsensitive(const std::string_view value,
                                       const std::string_view needle) noexcept {
    if (needle.empty()) {
        return true;
    }
    if (needle.size() > value.size()) {
        return false;
    }
    for (std::size_t offset = 0U; offset + needle.size() <= value.size(); ++offset) {
        if (startsWithInsensitive(value.substr(offset), needle)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::optional<TextFormat> formatFromPath(const std::filesystem::path& path) {
    const auto filename = lowerAscii(path.filename().string());
    if (filename == "makefile" || filename == "gnumakefile") {
        return TF::Makefile;
    }
    if (filename == "dockerfile" || filename.starts_with("dockerfile.")) {
        return TF::Dockerfile;
    }
    if (filename == "cmakelists.txt") {
        return TF::CMake;
    }
    if (filename == ".env" || filename.starts_with(".env.")) {
        return TF::Env;
    }
    if (filename == "readme" || filename.starts_with("readme.")) {
        const auto extension = lowerAscii(path.extension().string());
        if (extension.empty() || extension == ".txt") {
            return TF::Markdown;
        }
    }

    const auto extension = lowerAscii(path.extension().string());
    for (const auto& entry : kExtensions) {
        if (extension == entry.extension) {
            return entry.format;
        }
    }

    if (extension == ".psm1" || extension == ".psd1") {
        return TF::PowerShell;
    }
    if (extension == ".sql") {
        return TF::Sql;
    }
    if (extension == ".cmake") {
        return TF::CMake;
    }
    if (extension == ".diff" || extension == ".patch") {
        return TF::Diff;
    }
    return std::nullopt;
}

class JsonParser final {
public:
    explicit JsonParser(const std::string_view text) noexcept : text_(text) {}

    [[nodiscard]] bool parse() noexcept {
        skipSpace();
        if (!parseValue(0U)) {
            return false;
        }
        skipSpace();
        return position_ == text_.size();
    }

private:
    static constexpr std::size_t maxDepth = 128U;

    void skipSpace() noexcept {
        while (position_ < text_.size() && asciiSpace(text_[position_])) {
            ++position_;
        }
    }

    [[nodiscard]] bool consume(const char expected) noexcept {
        if (position_ >= text_.size() || text_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    [[nodiscard]] bool parseValue(const std::size_t depth) noexcept {
        if (depth > maxDepth) {
            return false;
        }
        skipSpace();
        if (position_ >= text_.size()) {
            return false;
        }
        switch (text_[position_]) {
        case '{':
            return parseObject(depth + 1U);
        case '[':
            return parseArray(depth + 1U);
        case '"':
            return parseString();
        case 't':
            return parseLiteral("true");
        case 'f':
            return parseLiteral("false");
        case 'n':
            return parseLiteral("null");
        default:
            return parseNumber();
        }
    }

    [[nodiscard]] bool parseObject(const std::size_t depth) noexcept {
        if (!consume('{')) {
            return false;
        }
        skipSpace();
        if (consume('}')) {
            return true;
        }
        for (;;) {
            skipSpace();
            if (!parseString()) {
                return false;
            }
            skipSpace();
            if (!consume(':') || !parseValue(depth)) {
                return false;
            }
            skipSpace();
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
        }
    }

    [[nodiscard]] bool parseArray(const std::size_t depth) noexcept {
        if (!consume('[')) {
            return false;
        }
        skipSpace();
        if (consume(']')) {
            return true;
        }
        for (;;) {
            if (!parseValue(depth)) {
                return false;
            }
            skipSpace();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
        }
    }

    [[nodiscard]] bool parseString() noexcept {
        if (!consume('"')) {
            return false;
        }
        while (position_ < text_.size()) {
            const auto ch = static_cast<unsigned char>(text_[position_++]);
            if (ch == '"') {
                return true;
            }
            if (ch < 0x20U) {
                return false;
            }
            if (ch != '\\') {
                continue;
            }
            if (position_ >= text_.size()) {
                return false;
            }
            const char escape = text_[position_++];
            if (escape == 'u') {
                for (std::size_t index = 0U; index < 4U; ++index) {
                    if (position_ >= text_.size()) {
                        return false;
                    }
                    const auto hex = static_cast<unsigned char>(text_[position_++]);
                    if (!std::isxdigit(hex)) {
                        return false;
                    }
                }
            } else if (escape != '"' && escape != '\\' && escape != '/' && escape != 'b' &&
                       escape != 'f' && escape != 'n' && escape != 'r' && escape != 't') {
                return false;
            }
        }
        return false;
    }

    [[nodiscard]] bool parseLiteral(const std::string_view literal) noexcept {
        if (text_.substr(position_, literal.size()) != literal) {
            return false;
        }
        position_ += literal.size();
        return true;
    }

    [[nodiscard]] bool parseNumber() noexcept {
        const auto start = position_;
        if (position_ < text_.size() && text_[position_] == '-') {
            ++position_;
        }
        if (position_ >= text_.size()) {
            return false;
        }
        if (text_[position_] == '0') {
            ++position_;
        } else {
            if (text_[position_] < '1' || text_[position_] > '9') {
                return false;
            }
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
                ++position_;
            }
        }
        if (position_ < text_.size() && text_[position_] == '.') {
            ++position_;
            const auto fractionStart = position_;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
                ++position_;
            }
            if (position_ == fractionStart) {
                return false;
            }
        }
        if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
            ++position_;
            if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) {
                ++position_;
            }
            const auto exponentStart = position_;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
                ++position_;
            }
            if (position_ == exponentStart) {
                return false;
            }
        }
        return position_ > start;
    }

    std::string_view text_;
    std::size_t position_{0U};
};

[[nodiscard]] bool validJson(const std::string_view text) noexcept {
    const auto value = trim(text);
    return !value.empty() && JsonParser(value).parse();
}

struct LineScan {
    std::size_t lines{0U};
    std::size_t nonEmpty{0U};
    std::size_t markdownSignals{0U};
    std::size_t logSignals{0U};
    std::size_t yamlSignals{0U};
    std::size_t tomlSignals{0U};
    std::size_t iniSignals{0U};
    std::size_t propertySignals{0U};
    std::size_t envSignals{0U};
};

[[nodiscard]] bool likelyTimestampPrefix(const std::string_view line) noexcept {
    if (line.size() >= 19U) {
        const bool iso = std::isdigit(static_cast<unsigned char>(line[0])) != 0 &&
                         std::isdigit(static_cast<unsigned char>(line[1])) != 0 &&
                         std::isdigit(static_cast<unsigned char>(line[2])) != 0 &&
                         std::isdigit(static_cast<unsigned char>(line[3])) != 0 &&
                         line[4] == '-' && line[7] == '-';
        if (iso) {
            return true;
        }
    }
    if (line.size() >= 10U && line.front() == '[' &&
        std::isdigit(static_cast<unsigned char>(line[1])) != 0) {
        return true;
    }
    return false;
}

[[nodiscard]] bool containsLogLevel(const std::string_view line) noexcept {
    constexpr std::array<std::string_view, 8> levels{
        " ERROR ", " WARN ", " WARNING ", " INFO ", " DEBUG ", " TRACE ", " FATAL ", " CRITICAL "};
    std::string padded;
    padded.reserve(line.size() + 2U);
    padded.push_back(' ');
    padded.append(line);
    padded.push_back(' ');
    for (const auto level : levels) {
        if (containsInsensitive(padded, level)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool looksLikeKey(const std::string_view text) noexcept {
    if (text.empty() || text.size() > 128U) {
        return false;
    }
    for (const char ch : text) {
        const auto byte = static_cast<unsigned char>(ch);
        if (std::isalnum(byte) == 0 && ch != '_' && ch != '-' && ch != '.' && ch != ' ') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] LineScan scanLines(const std::string_view text, const bool completeSample) {
    LineScan scan;
    std::size_t start = 0U;
    constexpr std::size_t maxLines = 128U;

    while (start <= text.size() && scan.lines < maxLines) {
        const auto end = text.find('\n', start);
        if (end == std::string_view::npos && !completeSample) {
            break;
        }
        const auto length = end == std::string_view::npos ? text.size() - start : end - start;
        auto line = trim(text.substr(start, length));
        ++scan.lines;
        if (!line.empty()) {
            ++scan.nonEmpty;

            if (line.starts_with("# ") || line.starts_with("## ") || line.starts_with("### ") ||
                line.starts_with("```" ) || line.starts_with("~~~") || line.starts_with("> ") ||
                line.starts_with("- [") || line.find("](") != std::string_view::npos) {
                ++scan.markdownSignals;
            }

            if (likelyTimestampPrefix(line) || containsLogLevel(line)) {
                ++scan.logSignals;
            }

            const auto colon = line.find(':');
            if ((line.starts_with("- ") || line == "---" || line == "...") ||
                (colon != std::string_view::npos && looksLikeKey(trim(line.substr(0U, colon))) &&
                 line.find("::") == std::string_view::npos)) {
                ++scan.yamlSignals;
            }

            if (line.front() == '[' && line.back() == ']' && line.size() >= 3U) {
                ++scan.tomlSignals;
                ++scan.iniSignals;
            }

            const auto equals = line.find('=');
            if (equals != std::string_view::npos && equals > 0U) {
                const auto key = trim(line.substr(0U, equals));
                if (looksLikeKey(key)) {
                    ++scan.tomlSignals;
                    ++scan.iniSignals;
                    ++scan.propertySignals;
                    bool envKey = !key.empty();
                    for (const char ch : key) {
                        const auto byte = static_cast<unsigned char>(ch);
                        if (std::isalnum(byte) == 0 && ch != '_') {
                            envKey = false;
                            break;
                        }
                    }
                    if (envKey) {
                        ++scan.envSignals;
                    }
                }
            }
        }

        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1U;
    }
    return scan;
}

[[nodiscard]] std::optional<std::size_t> delimitedFieldCount(const std::string_view line,
                                                             const char delimiter) noexcept {
    bool quoted = false;
    std::size_t fields = 1U;
    for (std::size_t index = 0U; index < line.size(); ++index) {
        const char ch = line[index];
        if (ch == '"') {
            if (quoted && index + 1U < line.size() && line[index + 1U] == '"') {
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (!quoted && ch == delimiter) {
            ++fields;
        }
    }
    if (quoted) {
        return std::nullopt;
    }
    return fields;
}

[[nodiscard]] std::uint8_t delimitedScore(const std::string_view text,
                                          const char delimiter,
                                          const bool completeSample) noexcept {
    std::size_t start = 0U;
    std::optional<std::size_t> expected;
    std::size_t matching = 0U;
    std::size_t checked = 0U;
    constexpr std::size_t maxLines = 32U;

    while (start <= text.size() && checked < maxLines) {
        const auto end = text.find('\n', start);
        if (end == std::string_view::npos && !completeSample) {
            break;
        }
        auto line = text.substr(start, end == std::string_view::npos ? text.size() - start : end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1U);
        }
        if (!trim(line).empty()) {
            const auto fields = delimitedFieldCount(line, delimiter);
            if (!fields || *fields < 2U) {
                return 0U;
            }
            if (!expected) {
                expected = fields;
            }
            if (*fields == *expected) {
                ++matching;
            }
            ++checked;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1U;
    }

    if (checked < 3U || matching != checked) {
        return 0U;
    }
    return expected.value_or(0U) >= 3U ? 88U : 80U;
}

[[nodiscard]] std::uint8_t jsonLinesScore(const std::string_view text,
                                          const bool completeSample) noexcept {
    std::size_t start = 0U;
    std::size_t valid = 0U;
    std::size_t checked = 0U;
    constexpr std::size_t maxLines = 32U;

    while (start <= text.size() && checked < maxLines) {
        const auto end = text.find('\n', start);
        if (end == std::string_view::npos && !completeSample) {
            break;
        }
        const auto line = trim(text.substr(start, end == std::string_view::npos
                                                     ? text.size() - start
                                                     : end - start));
        if (!line.empty()) {
            ++checked;
            if (validJson(line)) {
                ++valid;
            } else {
                return 0U;
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1U;
    }
    return checked >= 2U && valid == checked ? 95U : 0U;
}

struct Candidate {
    TextFormat format{TF::PlainText};
    std::uint8_t score{0U};
};

void consider(Candidate& best, const TextFormat format, const std::uint8_t score) noexcept {
    if (score > best.score) {
        best = {format, score};
    }
}

[[nodiscard]] Candidate detectContent(const std::string_view sample,
                                      const bool completeSample) {
    Candidate best;
    const auto text = trim(sample);
    if (text.empty()) {
        return best;
    }

    if (startsWithInsensitive(text, "<!doctype html") || startsWithInsensitive(text, "<html") ||
        containsInsensitive(text.substr(0U, std::min<std::size_t>(text.size(), 4096U)), "<html")) {
        consider(best, TF::Html, 98U);
    } else if (startsWithInsensitive(text, "<?xml") ||
               (text.front() == '<' && text.find('>') != std::string_view::npos &&
                text.find("</") != std::string_view::npos)) {
        consider(best, TF::Xml, startsWithInsensitive(text, "<?xml") ? 98U : 86U);
    }

    if (validJson(text)) {
        const auto first = text.front();
        consider(best, TF::Json, first == '{' || first == '[' ? 100U : 94U);
    } else {
        consider(best, TF::JsonLines, jsonLinesScore(text, completeSample));
    }

    if (startsWithInsensitive(text, "#!/usr/bin/env python") ||
        startsWithInsensitive(text, "#!/usr/bin/python")) {
        consider(best, TF::Python, 100U);
    } else if (startsWithInsensitive(text, "#!/bin/sh") ||
               startsWithInsensitive(text, "#!/bin/bash") ||
               startsWithInsensitive(text, "#!/usr/bin/env bash") ||
               startsWithInsensitive(text, "#!/usr/bin/env sh") ||
               startsWithInsensitive(text, "#!/usr/bin/env zsh")) {
        consider(best, TF::Shell, 100U);
    } else if (startsWithInsensitive(text, "#!/usr/bin/env node") ||
               startsWithInsensitive(text, "#!/usr/bin/node")) {
        consider(best, TF::JavaScript, 100U);
    }

    if (text.starts_with("diff --git ") || text.starts_with("--- a/") ||
        (text.find("@@ -") != std::string_view::npos && text.find(" +++ ") != std::string_view::npos)) {
        consider(best, TF::Diff, 96U);
    }

    const auto csv = delimitedScore(text, ',', completeSample);
    const auto tsv = delimitedScore(text, '\t', completeSample);
    if (csv > 0U || tsv > 0U) {
        if (tsv > csv) {
            consider(best, TF::Tsv, tsv);
        } else {
            consider(best, TF::Csv, csv);
        }
    }

    const auto lines = scanLines(text, completeSample);
    if (lines.nonEmpty >= 2U) {
        if (lines.markdownSignals >= 2U ||
            (lines.markdownSignals >= 1U && lines.nonEmpty <= 4U)) {
            consider(best, TF::Markdown, lines.markdownSignals >= 3U ? 90U : 78U);
        }
        if (lines.logSignals >= 2U && lines.logSignals * 2U >= lines.nonEmpty) {
            consider(best, TF::Log, 90U);
        }
        if (lines.yamlSignals >= 3U && lines.yamlSignals * 2U >= lines.nonEmpty) {
            consider(best, TF::Yaml, 78U);
        }
        if (lines.tomlSignals >= 3U && lines.tomlSignals * 2U >= lines.nonEmpty) {
            consider(best, TF::Toml, 80U);
        }
        if (lines.iniSignals >= 3U && lines.iniSignals * 2U >= lines.nonEmpty) {
            consider(best, TF::Ini, 76U);
        }
        if (lines.propertySignals >= 3U && lines.propertySignals * 2U >= lines.nonEmpty) {
            consider(best, TF::Properties, 74U);
        }
        if (lines.envSignals >= 3U && lines.envSignals * 2U >= lines.nonEmpty) {
            consider(best, TF::Env, 74U);
        }
    }

    return best;
}

[[nodiscard]] constexpr bool sameKeyValueFamily(const TextFormat left,
                                                const TextFormat right) noexcept {
    const auto isKeyValue = [](const TextFormat value) constexpr noexcept {
        return value == TF::Toml || value == TF::Ini || value == TF::Properties ||
               value == TF::Env;
    };
    return isKeyValue(left) && isKeyValue(right);
}

[[nodiscard]] std::uint8_t extensionConfidence(const TextFormat format) noexcept {
    switch (format) {
    case TF::PlainText:
        return 45U;
    case TF::Ini:
        return 58U;
    case TF::Log:
    case TF::Markdown:
    case TF::Properties:
    case TF::Env:
        return 65U;
    default:
        return 72U;
    }
}

}

DetectionResult FormatDetector::detect(const std::filesystem::path& path,
                                       const std::string_view utf8Sample,
                                       const bool completeSample) {
    DetectionResult result;
    const auto extensionFormat = formatFromPath(path);
    if (extensionFormat) {
        result.format = *extensionFormat;
        result.confidence = extensionConfidence(*extensionFormat);
        result.matchedExtension = true;
    }

    const auto content = detectContent(utf8Sample, completeSample);
    if (extensionFormat && content.score > 0U &&
        sameKeyValueFamily(*extensionFormat, content.format)) {
        result.format = *extensionFormat;
        const auto boosted = static_cast<unsigned int>(result.confidence) + 18U;
        result.confidence = static_cast<std::uint8_t>(std::min(boosted, 100U));
        result.matchedContent = true;
    } else if (content.score > result.confidence ||
               (content.score == result.confidence && content.format == result.format)) {
        result.format = content.format;
        result.confidence = content.score;
        result.matchedContent = content.score > 0U;
    } else if (content.score > 0U && content.format == result.format) {
        const auto boosted = static_cast<unsigned int>(result.confidence) + 18U;
        result.confidence = static_cast<std::uint8_t>(std::min(boosted, 100U));
        result.matchedContent = true;
    }

    return result;
}

const FormatDescriptor& FormatDetector::descriptor(const TextFormat format) noexcept {
    const auto index = static_cast<std::size_t>(format);
    if (index < kDescriptors.size()) {
        return kDescriptors[index];
    }
    return kDescriptors.front();
}

std::span<const FormatDescriptor> FormatDetector::descriptors() noexcept {
    return kDescriptors;
}

}
