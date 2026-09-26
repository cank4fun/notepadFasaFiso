#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace nff::formats {

enum class TextFormat : std::uint8_t {
    PlainText,
    Log,
    Markdown,
    Json,
    JsonLines,
    Xml,
    Html,
    Css,
    Yaml,
    Toml,
    Ini,
    Properties,
    Env,
    Csv,
    Tsv,
    JavaScript,
    TypeScript,
    C,
    Cpp,
    CSharp,
    Java,
    Python,
    Rust,
    Go,
    Shell,
    Batch,
    PowerShell,
    Lua,
    Sql,
    CMake,
    Makefile,
    Dockerfile,
    Diff
};

struct FormatCapabilities {
    bool syntaxHighlighting{false};
    bool structured{false};
    bool validation{false};
    bool prettyPrint{false};
    bool minify{false};
    bool tableView{false};
    bool markupPreview{false};
    bool lineOriented{false};
};

struct FormatDescriptor {
    TextFormat format{TextFormat::PlainText};
    std::string_view name{"Plain Text"};
    std::string_view canonicalExtension{};
    FormatCapabilities capabilities{};
};

struct DetectionResult {
    TextFormat format{TextFormat::PlainText};
    std::uint8_t confidence{0};
    bool matchedExtension{false};
    bool matchedContent{false};
};

class FormatDetector final {
public:
    [[nodiscard]] static DetectionResult detect(const std::filesystem::path& path,
                                                std::string_view utf8Sample,
                                                bool completeSample = true);

    [[nodiscard]] static const FormatDescriptor& descriptor(TextFormat format) noexcept;
    [[nodiscard]] static std::span<const FormatDescriptor> descriptors() noexcept;
};

}
