#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace nff::metadata {

enum class TextColorEditPolicy : std::uint8_t {
    AdjustRanges,
    PreserveOffsets,
    PreserveState,
};

using AppearanceEditPolicy = TextColorEditPolicy;
using FontFamilyId = std::uint32_t;

struct AppearanceStyle final {
    std::optional<std::uint32_t> foregroundArgb;
    std::optional<FontFamilyId> fontFamilyId;
    std::optional<std::uint8_t> fontSizePoints;
    bool spoiler{false};

    [[nodiscard]] bool empty() const noexcept {
        return !foregroundArgb.has_value() && !fontFamilyId.has_value() &&
               !fontSizePoints.has_value() && !spoiler;
    }

    friend bool operator==(const AppearanceStyle&, const AppearanceStyle&) = default;
};

struct TextAppearanceSpan final {
    std::uint64_t begin{0};
    std::uint64_t end{0};
    AppearanceStyle style;

    friend bool operator==(const TextAppearanceSpan&, const TextAppearanceSpan&) = default;
};

class TextAppearanceMap final {
public:
    static constexpr std::uint8_t minimumFontSizePoints = 5U;
    static constexpr std::uint8_t maximumFontSizePoints = 30U;
    static constexpr std::size_t maximumFontFamilyBytes = 1024U;

    [[nodiscard]] const std::vector<TextAppearanceSpan>& spans() const noexcept;
    [[nodiscard]] const std::vector<std::string>& fontFamilies() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool hasSpoilers() const noexcept;
    [[nodiscard]] bool intersects(std::uint64_t begin, std::uint64_t end) const noexcept;
    [[nodiscard]] AppearanceStyle styleAt(std::uint64_t offset) const noexcept;
    [[nodiscard]] std::optional<FontFamilyId> internFontFamily(std::string_view family);
    [[nodiscard]] std::string_view fontFamily(FontFamilyId id) const noexcept;

    void clear() noexcept;
    void setForeground(std::uint64_t begin, std::uint64_t end, std::uint32_t argb);
    void clearForeground(std::uint64_t begin, std::uint64_t end);
    [[nodiscard]] bool setFontFamily(std::uint64_t begin,
                                     std::uint64_t end,
                                     FontFamilyId family);
    void clearFontFamily(std::uint64_t begin, std::uint64_t end);
    [[nodiscard]] bool setFontSize(std::uint64_t begin,
                                   std::uint64_t end,
                                   std::uint8_t points);
    void clearFontSize(std::uint64_t begin, std::uint64_t end);
    void setSpoiler(std::uint64_t begin, std::uint64_t end, bool enabled);
    void reset(std::uint64_t begin, std::uint64_t end);

    void applyEdit(std::uint64_t offset,
                   std::uint64_t erasedBytes,
                   std::uint64_t insertedBytes,
                   AppearanceEditPolicy policy = AppearanceEditPolicy::AdjustRanges);

    [[nodiscard]] std::vector<TextAppearanceSpan> fragment(std::uint64_t begin,
                                                           std::uint64_t end) const;
    [[nodiscard]] bool replaceRange(std::uint64_t begin,
                                    std::uint64_t end,
                                    std::vector<TextAppearanceSpan> replacement);
    [[nodiscard]] bool replace(std::vector<TextAppearanceSpan> spans,
                               std::vector<std::string> fontFamilies = {});

private:
    void normalize();
    void compactSorted();
    [[nodiscard]] bool validStyle(const AppearanceStyle& style) const noexcept;

    std::vector<TextAppearanceSpan> spans_;
    std::vector<std::string> fontFamilies_;
    bool hasSpoilers_{false};
};

struct TextColorSpan final {
    std::uint64_t begin{0};
    std::uint64_t end{0};
    std::uint32_t argb{0xFFFFFFFFU};

    friend bool operator==(const TextColorSpan&, const TextColorSpan&) = default;
};

class TextColorMap final {
public:
    [[nodiscard]] const std::vector<TextColorSpan>& spans() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool intersects(std::uint64_t begin, std::uint64_t end) const noexcept;
    void clear() noexcept;
    void setColor(std::uint64_t begin, std::uint64_t end, std::uint32_t argb);
    void clearColor(std::uint64_t begin, std::uint64_t end);
    void applyEdit(std::uint64_t offset,
                   std::uint64_t erasedBytes,
                   std::uint64_t insertedBytes,
                   TextColorEditPolicy policy = TextColorEditPolicy::AdjustRanges);
    [[nodiscard]] bool replace(std::vector<TextColorSpan> spans);

private:
    void normalize();
    std::vector<TextColorSpan> spans_;
};

struct DocumentMetadata final {
    std::uint64_t textHash{0};
    std::uint64_t textBytes{0};
    TextAppearanceMap appearance;
    TextColorMap colors;
};

struct MetadataLoadResult final {
    DocumentMetadata metadata;
    bool stale{false};
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

class MetadataStore final {
public:
    static constexpr std::uint32_t currentSchemaVersion = 2U;
    static constexpr std::size_t defaultMaximumMetadataBytes = 64U * 1024U * 1024U;

    explicit MetadataStore(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] std::filesystem::path metadataPath(const std::filesystem::path& documentPath) const;
    [[nodiscard]] std::error_code save(const std::filesystem::path& documentPath,
                                       std::string_view utf8Text,
                                       const TextAppearanceMap& appearance);
    [[nodiscard]] std::error_code save(const std::filesystem::path& documentPath,
                                       std::string_view utf8Text,
                                       const TextColorMap& colors);
    [[nodiscard]] MetadataLoadResult load(
        const std::filesystem::path& documentPath,
        std::string_view currentUtf8Text,
        std::size_t maximumBytes = defaultMaximumMetadataBytes) const;
    [[nodiscard]] std::error_code erase(const std::filesystem::path& documentPath) noexcept;

private:
    std::filesystem::path root_;
};

}
