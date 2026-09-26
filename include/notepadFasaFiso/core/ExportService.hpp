#pragma once

#include "notepadFasaFiso/core/SaveOptions.hpp"
#include "notepadFasaFiso/formats/FormatTransform.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace nff::core {

enum class ContentTransform : std::uint8_t {
    None,
    JsonPretty,
    JsonMinify,
    CsvToTsv,
    TsvToCsv
};

struct ExportOptions {
    SaveOptions save{};
    ContentTransform transform{ContentTransform::None};
    std::uint8_t jsonIndentWidth{2};
    formats::TransformLimits limits{};
};

struct ExportRenderResult {
    std::string text;
    std::error_code error{};
    formats::ValidationResult validation{};

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

struct ExportResult {
    std::uintmax_t bytesWritten{0};
    std::error_code error{};
    formats::ValidationResult validation{};

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

class ExportService final {
public:
    [[nodiscard]] static ExportRenderResult render(std::string_view utf8Text,
                                                   const ExportOptions& options = {});
    [[nodiscard]] static ExportResult write(const std::filesystem::path& path,
                                            std::string_view utf8Text,
                                            const ExportOptions& options = {});
};

}
