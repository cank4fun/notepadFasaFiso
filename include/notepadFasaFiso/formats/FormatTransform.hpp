#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

namespace nff::formats {

enum class FormatTransformErrc : std::uint8_t {
    InvalidJson = 1,
    InvalidDelimitedText,
    OutputLimitExceeded,
    UnsupportedTransform
};

[[nodiscard]] std::error_code make_error_code(FormatTransformErrc error) noexcept;

struct ValidationResult {
    bool valid{false};
    std::size_t offset{0};
    std::size_t line{1};
    std::size_t column{1};
    std::string message;
};

struct TransformLimits {
    std::size_t maxNesting{256};
    std::size_t maxOutputBytes{512ULL * 1024ULL * 1024ULL};
};

struct TransformResult {
    std::string text;
    std::error_code error{};
    ValidationResult validation{};

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

struct JsonFormatOptions {
    std::uint8_t indentWidth{2};
    TransformLimits limits{};
};

struct DelimitedTextOptions {
    char sourceDelimiter{','};
    char targetDelimiter{'\t'};
    TransformLimits limits{};
};

class FormatTransform final {
public:
    [[nodiscard]] static ValidationResult validateJson(std::string_view text,
                                                       const TransformLimits& limits = {});
    [[nodiscard]] static TransformResult prettyJson(std::string_view text,
                                                    const JsonFormatOptions& options = {});
    [[nodiscard]] static TransformResult minifyJson(std::string_view text,
                                                    const TransformLimits& limits = {});

    [[nodiscard]] static TransformResult convertDelimited(
        std::string_view text,
        const DelimitedTextOptions& options = {});
};

}

namespace std {
template <>
struct is_error_code_enum<nff::formats::FormatTransformErrc> : true_type {};
}
