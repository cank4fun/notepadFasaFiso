#include "notepadFasaFiso/core/ExportService.hpp"

#include "notepadFasaFiso/core/TextTransform.hpp"
#include "notepadFasaFiso/encoding/TextCodec.hpp"
#include "notepadFasaFiso/storage/FileWriter.hpp"

namespace nff::core {
namespace {

[[nodiscard]] constexpr bool supportsBom(const encoding::Encoding value) noexcept {
    switch (value) {
    case encoding::Encoding::Utf8:
    case encoding::Encoding::Utf16LE:
    case encoding::Encoding::Utf16BE:
    case encoding::Encoding::Utf32LE:
    case encoding::Encoding::Utf32BE:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] formats::TransformResult applyContentTransform(const std::string_view text,
                                                             const ExportOptions& options) {
    switch (options.transform) {
    case ContentTransform::None: {
        formats::TransformResult result;
        result.text.assign(text);
        result.validation.valid = true;
        result.validation.offset = text.size();
        return result;
    }
    case ContentTransform::JsonPretty:
        return formats::FormatTransform::prettyJson(
            text, formats::JsonFormatOptions{options.jsonIndentWidth, options.limits});
    case ContentTransform::JsonMinify:
        return formats::FormatTransform::minifyJson(text, options.limits);
    case ContentTransform::CsvToTsv:
        return formats::FormatTransform::convertDelimited(
            text, formats::DelimitedTextOptions{',', '\t', options.limits});
    case ContentTransform::TsvToCsv:
        return formats::FormatTransform::convertDelimited(
            text, formats::DelimitedTextOptions{'\t', ',', options.limits});
    }

    formats::TransformResult result;
    result.error = formats::make_error_code(formats::FormatTransformErrc::UnsupportedTransform);
    return result;
}

}

ExportRenderResult ExportService::render(const std::string_view utf8Text,
                                         const ExportOptions& options) {
    const auto transformed = applyContentTransform(utf8Text, options);
    if (!transformed) {
        return {{}, transformed.error, transformed.validation};
    }

    ExportRenderResult result;
    result.text = TextTransform::normalizeLineEndings(transformed.text, options.save.lineEnding);
    result.validation = transformed.validation;
    return result;
}

ExportResult ExportService::write(const std::filesystem::path& path,
                                  const std::string_view utf8Text,
                                  const ExportOptions& options) {
    if (path.empty()) {
        return {0U, std::make_error_code(std::errc::invalid_argument), {}};
    }

    const auto rendered = render(utf8Text, options);
    if (!rendered) {
        return {0U, rendered.error, rendered.validation};
    }

    const auto targetEncoding = options.save.encoding.value_or(encoding::Encoding::Utf8);
    const auto targetBom = options.save.writeBom.value_or(false);
    if (targetBom && !supportsBom(targetEncoding)) {
        return {0U, std::make_error_code(std::errc::invalid_argument), rendered.validation};
    }

    const auto encoded = encoding::TextCodec::encode(rendered.text, targetEncoding, targetBom);
    if (!encoded) {
        return {0U, encoded.error, rendered.validation};
    }

    const auto error = storage::FileWriter::writeAtomically(path, encoded.bytes);
    if (error) {
        return {0U, error, rendered.validation};
    }

    return {static_cast<std::uintmax_t>(encoded.bytes.size()), {}, rendered.validation};
}

}
