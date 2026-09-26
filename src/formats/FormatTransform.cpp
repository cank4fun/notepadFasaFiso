#include "notepadFasaFiso/formats/FormatTransform.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string_view>
#include <vector>

namespace nff::formats {
namespace {

class TransformErrorCategory final : public std::error_category {
public:
    [[nodiscard]] const char* name() const noexcept override { return "notepadFasaFiso.format"; }

    [[nodiscard]] std::string message(const int condition) const override {
        switch (static_cast<FormatTransformErrc>(condition)) {
        case FormatTransformErrc::InvalidJson:
            return "invalid JSON";
        case FormatTransformErrc::InvalidDelimitedText:
            return "invalid delimited text";
        case FormatTransformErrc::OutputLimitExceeded:
            return "format transform output limit exceeded";
        case FormatTransformErrc::UnsupportedTransform:
            return "unsupported format transform";
        }
        return "unknown format transform error";
    }
};

[[nodiscard]] const std::error_category& transformCategory() noexcept {
    static TransformErrorCategory category;
    return category;
}

[[nodiscard]] constexpr bool jsonSpace(const char ch) noexcept {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

[[nodiscard]] constexpr bool hexDigit(const char ch) noexcept {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
           (ch >= 'A' && ch <= 'F');
}

class JsonTransformer final {
public:
    JsonTransformer(const std::string_view input,
                    const bool pretty,
                    const std::uint8_t indentWidth,
                    const TransformLimits& limits)
        : input_(input), pretty_(pretty), indentWidth_(indentWidth), limits_(limits) {
    }

    [[nodiscard]] TransformResult run() {
        if (emit_) {
            output_.reserve(input_.size());
        }
        skipSpace();
        if (!parseValue(0U)) {
            return failure();
        }
        skipSpace();
        if (position_ != input_.size()) {
            setError("unexpected content after JSON value");
            return failure();
        }

        TransformResult result;
        result.text = std::move(output_);
        result.validation = makeValidation(true, input_.size(), {});
        return result;
    }

    [[nodiscard]] ValidationResult validateOnly() {
        emit_ = false;
        skipSpace();
        if (!parseValue(0U)) {
            return makeValidation(false, errorOffset_, errorMessage_);
        }
        skipSpace();
        if (position_ != input_.size()) {
            setError("unexpected content after JSON value");
            return makeValidation(false, errorOffset_, errorMessage_);
        }
        return makeValidation(true, input_.size(), {});
    }

private:
    void skipSpace() noexcept {
        while (position_ < input_.size() && jsonSpace(input_[position_])) {
            ++position_;
        }
    }

    [[nodiscard]] bool canAppend(const std::size_t amount) {
        if (!emit_) {
            return true;
        }
        if (limits_.maxOutputBytes == 0U) {
            return true;
        }
        if (amount > limits_.maxOutputBytes || output_.size() > limits_.maxOutputBytes - amount) {
            outputLimitExceeded_ = true;
            setError("output limit exceeded");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool append(const char ch) {
        if (!emit_) {
            return true;
        }
        if (!canAppend(1U)) {
            return false;
        }
        output_.push_back(ch);
        return true;
    }

    [[nodiscard]] bool append(const std::string_view text) {
        if (!emit_) {
            return true;
        }
        if (!canAppend(text.size())) {
            return false;
        }
        output_.append(text);
        return true;
    }

    [[nodiscard]] bool appendIndent(const std::size_t depth) {
        if (!pretty_ || !emit_) {
            return true;
        }
        const auto width = static_cast<std::size_t>(indentWidth_);
        if (width != 0U && depth > std::numeric_limits<std::size_t>::max() / width) {
            outputLimitExceeded_ = true;
            setError("output limit exceeded");
            return false;
        }
        const auto count = depth * width;
        if (!canAppend(count)) {
            return false;
        }
        output_.append(count, ' ');
        return true;
    }

    [[nodiscard]] bool appendNewlineAndIndent(const std::size_t depth) {
        if (!pretty_) {
            return true;
        }
        return append('\n') && appendIndent(depth);
    }

    [[nodiscard]] bool consume(const char expected) noexcept {
        if (position_ >= input_.size() || input_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    [[nodiscard]] bool parseValue(const std::size_t depth) {
        if (depth > limits_.maxNesting) {
            setError("JSON nesting limit exceeded");
            return false;
        }
        skipSpace();
        if (position_ >= input_.size()) {
            setError("expected JSON value");
            return false;
        }

        switch (input_[position_]) {
        case '{':
            return parseObject(depth);
        case '[':
            return parseArray(depth);
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

    [[nodiscard]] bool parseObject(const std::size_t depth) {
        ++position_;
        if (!append('{')) {
            return false;
        }
        skipSpace();
        if (consume('}')) {
            return append('}');
        }

        if (!appendNewlineAndIndent(depth + 1U)) {
            return false;
        }

        for (;;) {
            skipSpace();
            if (position_ >= input_.size() || input_[position_] != '"') {
                setError("expected JSON object key");
                return false;
            }
            if (!parseString()) {
                return false;
            }
            skipSpace();
            if (!consume(':')) {
                setError("expected ':' after JSON object key");
                return false;
            }
            if (!append(pretty_ ? ": " : ":")) {
                return false;
            }
            if (!parseValue(depth + 1U)) {
                return false;
            }
            skipSpace();
            if (consume('}')) {
                return appendNewlineAndIndent(depth) && append('}');
            }
            if (!consume(',')) {
                setError("expected ',' or '}' in JSON object");
                return false;
            }
            if (!append(',') || !appendNewlineAndIndent(depth + 1U)) {
                return false;
            }
        }
    }

    [[nodiscard]] bool parseArray(const std::size_t depth) {
        ++position_;
        if (!append('[')) {
            return false;
        }
        skipSpace();
        if (consume(']')) {
            return append(']');
        }

        if (!appendNewlineAndIndent(depth + 1U)) {
            return false;
        }

        for (;;) {
            if (!parseValue(depth + 1U)) {
                return false;
            }
            skipSpace();
            if (consume(']')) {
                return appendNewlineAndIndent(depth) && append(']');
            }
            if (!consume(',')) {
                setError("expected ',' or ']' in JSON array");
                return false;
            }
            if (!append(',') || !appendNewlineAndIndent(depth + 1U)) {
                return false;
            }
        }
    }

    [[nodiscard]] bool parseString() {
        if (position_ >= input_.size() || input_[position_] != '"') {
            setError("expected JSON string");
            return false;
        }
        const auto start = position_++;

        while (position_ < input_.size()) {
            const auto ch = static_cast<unsigned char>(input_[position_++]);
            if (ch == static_cast<unsigned char>('"')) {
                return append(input_.substr(start, position_ - start));
            }
            if (ch < 0x20U) {
                setErrorAt(position_ - 1U, "unescaped control character in JSON string");
                return false;
            }
            if (ch != static_cast<unsigned char>('\\')) {
                continue;
            }
            if (position_ >= input_.size()) {
                setError("unterminated JSON escape sequence");
                return false;
            }
            const char escape = input_[position_++];
            if (escape == 'u') {
                if (input_.size() - position_ < 4U) {
                    setError("truncated JSON unicode escape");
                    return false;
                }
                for (std::size_t index = 0U; index < 4U; ++index) {
                    if (!hexDigit(input_[position_ + index])) {
                        setErrorAt(position_ + index, "invalid JSON unicode escape");
                        return false;
                    }
                }
                position_ += 4U;
            } else if (escape != '"' && escape != '\\' && escape != '/' && escape != 'b' &&
                       escape != 'f' && escape != 'n' && escape != 'r' && escape != 't') {
                setErrorAt(position_ - 1U, "invalid JSON escape sequence");
                return false;
            }
        }

        setError("unterminated JSON string");
        return false;
    }

    [[nodiscard]] bool parseLiteral(const std::string_view literal) {
        if (input_.substr(position_, literal.size()) != literal) {
            setError("invalid JSON literal");
            return false;
        }
        position_ += literal.size();
        return append(literal);
    }

    [[nodiscard]] bool parseNumber() {
        const auto start = position_;
        if (position_ < input_.size() && input_[position_] == '-') {
            ++position_;
        }
        if (position_ >= input_.size()) {
            setError("invalid JSON number");
            return false;
        }

        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                setError("leading zero in JSON number");
                return false;
            }
        } else {
            if (input_[position_] < '1' || input_[position_] > '9') {
                setError("expected JSON value");
                return false;
            }
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
        }

        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            const auto fractionStart = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
            if (position_ == fractionStart) {
                setError("invalid JSON fraction");
                return false;
            }
        }

        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            const auto exponentStart = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
            if (position_ == exponentStart) {
                setError("invalid JSON exponent");
                return false;
            }
        }

        return append(input_.substr(start, position_ - start));
    }

    void setError(const std::string_view message) {
        setErrorAt(position_, message);
    }

    void setErrorAt(const std::size_t offset, const std::string_view message) {
        if (!errorMessage_.empty()) {
            return;
        }
        errorOffset_ = std::min(offset, input_.size());
        errorMessage_.assign(message);
    }

    [[nodiscard]] ValidationResult makeValidation(const bool valid,
                                                  const std::size_t offset,
                                                  std::string message) const {
        ValidationResult result;
        result.valid = valid;
        result.offset = std::min(offset, input_.size());
        result.message = std::move(message);

        for (std::size_t index = 0U; index < result.offset; ++index) {
            if (input_[index] == '\n') {
                ++result.line;
                result.column = 1U;
            } else {
                ++result.column;
            }
        }
        return result;
    }

    [[nodiscard]] TransformResult failure() const {
        TransformResult result;
        result.error = make_error_code(outputLimitExceeded_ ? FormatTransformErrc::OutputLimitExceeded
                                                            : FormatTransformErrc::InvalidJson);
        result.validation = makeValidation(false, errorOffset_, errorMessage_);
        return result;
    }

    std::string_view input_;
    bool pretty_{false};
    std::uint8_t indentWidth_{2};
    TransformLimits limits_{};
    std::size_t position_{0U};
    std::string output_;
    bool emit_{true};
    bool outputLimitExceeded_{false};
    std::size_t errorOffset_{0U};
    std::string errorMessage_;
};

enum class FieldBoundary : std::uint8_t {
    Delimiter,
    RecordEnd,
    End
};

[[nodiscard]] bool appendChecked(std::string& output,
                                 const std::string_view text,
                                 const TransformLimits& limits) {
    if (limits.maxOutputBytes != 0U &&
        (text.size() > limits.maxOutputBytes || output.size() > limits.maxOutputBytes - text.size())) {
        return false;
    }
    output.append(text);
    return true;
}

[[nodiscard]] bool appendChecked(std::string& output,
                                 const char ch,
                                 const TransformLimits& limits) {
    return appendChecked(output, std::string_view{&ch, 1U}, limits);
}

[[nodiscard]] bool fieldNeedsQuotes(const std::string_view field, const char delimiter) noexcept {
    return field.find(delimiter) != std::string_view::npos ||
           field.find('"') != std::string_view::npos ||
           field.find('\r') != std::string_view::npos ||
           field.find('\n') != std::string_view::npos;
}

class DelimitedTransformer final {
public:
    DelimitedTransformer(const std::string_view input, const DelimitedTextOptions& options)
        : input_(input), options_(options) {
        output_.reserve(input.size());
    }

    [[nodiscard]] TransformResult run() {
        if (!validDelimiter(options_.sourceDelimiter) || !validDelimiter(options_.targetDelimiter)) {
            return failure(0U, "invalid delimiter");
        }
        if (input_.empty()) {
            TransformResult result;
            result.validation.valid = true;
            return result;
        }

        for (;;) {
            std::string field;
            FieldBoundary boundary{FieldBoundary::End};
            if (!parseField(field, boundary)) {
                return failure(errorOffset_, errorMessage_);
            }
            if (!appendField(field)) {
                TransformResult result;
                result.error = make_error_code(FormatTransformErrc::OutputLimitExceeded);
                return result;
            }

            if (boundary == FieldBoundary::Delimiter) {
                if (!appendChecked(output_, options_.targetDelimiter, options_.limits)) {
                    TransformResult result;
                    result.error = make_error_code(FormatTransformErrc::OutputLimitExceeded);
                    return result;
                }
                continue;
            }
            if (boundary == FieldBoundary::RecordEnd) {
                if (!appendChecked(output_, '\n', options_.limits)) {
                    TransformResult result;
                    result.error = make_error_code(FormatTransformErrc::OutputLimitExceeded);
                    return result;
                }
                if (position_ < input_.size()) {
                    continue;
                }
            }
            break;
        }

        TransformResult result;
        result.text = std::move(output_);
        result.validation.valid = true;
        result.validation.offset = input_.size();
        return result;
    }

private:
    [[nodiscard]] static constexpr bool validDelimiter(const char delimiter) noexcept {
        return delimiter != '\r' && delimiter != '\n' && delimiter != '"' && delimiter != '\0';
    }

    [[nodiscard]] bool parseField(std::string& field, FieldBoundary& boundary) {
        if (position_ >= input_.size()) {
            boundary = FieldBoundary::End;
            return true;
        }

        if (input_[position_] == '"') {
            ++position_;
            if (!parseQuotedField(field)) {
                return false;
            }
            if (position_ >= input_.size()) {
                boundary = FieldBoundary::End;
                return true;
            }
            if (input_[position_] == options_.sourceDelimiter) {
                ++position_;
                boundary = FieldBoundary::Delimiter;
                return true;
            }
            if (isLineEnding(input_[position_])) {
                consumeLineEnding();
                boundary = FieldBoundary::RecordEnd;
                return true;
            }
            setError(position_, "unexpected character after closing quote");
            return false;
        }

        while (position_ < input_.size()) {
            const char ch = input_[position_];
            if (ch == '"') {
                setError(position_, "quote must begin a field");
                return false;
            }
            if (ch == options_.sourceDelimiter) {
                ++position_;
                boundary = FieldBoundary::Delimiter;
                return true;
            }
            if (isLineEnding(ch)) {
                consumeLineEnding();
                boundary = FieldBoundary::RecordEnd;
                return true;
            }
            field.push_back(ch);
            ++position_;
        }

        boundary = FieldBoundary::End;
        return true;
    }

    [[nodiscard]] bool parseQuotedField(std::string& field) {
        while (position_ < input_.size()) {
            const char ch = input_[position_++];
            if (ch != '"') {
                field.push_back(ch);
                continue;
            }
            if (position_ < input_.size() && input_[position_] == '"') {
                field.push_back('"');
                ++position_;
                continue;
            }
            return true;
        }
        setError(input_.size(), "unterminated quoted field");
        return false;
    }

    [[nodiscard]] bool appendField(const std::string_view field) {
        if (!fieldNeedsQuotes(field, options_.targetDelimiter)) {
            return appendChecked(output_, field, options_.limits);
        }
        if (!appendChecked(output_, '"', options_.limits)) {
            return false;
        }
        for (const char ch : field) {
            if (ch == '"' && !appendChecked(output_, '"', options_.limits)) {
                return false;
            }
            if (!appendChecked(output_, ch, options_.limits)) {
                return false;
            }
        }
        return appendChecked(output_, '"', options_.limits);
    }

    [[nodiscard]] static constexpr bool isLineEnding(const char ch) noexcept {
        return ch == '\r' || ch == '\n';
    }

    void consumeLineEnding() noexcept {
        if (input_[position_] == '\r' && position_ + 1U < input_.size() &&
            input_[position_ + 1U] == '\n') {
            position_ += 2U;
        } else {
            ++position_;
        }
    }

    void setError(const std::size_t offset, const std::string_view message) {
        errorOffset_ = offset;
        errorMessage_.assign(message);
    }

    [[nodiscard]] TransformResult failure(const std::size_t offset,
                                          const std::string_view message) const;

    std::string_view input_;
    DelimitedTextOptions options_{};
    std::size_t position_{0U};
    std::string output_;
    std::size_t errorOffset_{0U};
    std::string errorMessage_;
};

[[nodiscard]] ValidationResult delimitedValidation(const std::string_view text,
                                                   const bool valid,
                                                   const std::size_t offset,
                                                   std::string message) {
    ValidationResult result;
    result.valid = valid;
    result.offset = std::min(offset, text.size());
    result.message = std::move(message);
    for (std::size_t index = 0U; index < result.offset; ++index) {
        if (text[index] == '\n') {
            ++result.line;
            result.column = 1U;
        } else {
            ++result.column;
        }
    }
    return result;
}

TransformResult DelimitedTransformer::failure(const std::size_t offset,
                                              const std::string_view message) const {
    TransformResult result;
    result.error = make_error_code(FormatTransformErrc::InvalidDelimitedText);
    result.validation = delimitedValidation(input_, false, offset, std::string{message});
    return result;
}

}

std::error_code make_error_code(const FormatTransformErrc error) noexcept {
    return {static_cast<int>(error), transformCategory()};
}

ValidationResult FormatTransform::validateJson(const std::string_view text,
                                               const TransformLimits& limits) {
    JsonTransformer transformer{text, false, 0U, limits};
    return transformer.validateOnly();
}

TransformResult FormatTransform::prettyJson(const std::string_view text,
                                            const JsonFormatOptions& options) {
    JsonTransformer transformer{text, true, options.indentWidth, options.limits};
    return transformer.run();
}

TransformResult FormatTransform::minifyJson(const std::string_view text,
                                            const TransformLimits& limits) {
    JsonTransformer transformer{text, false, 0U, limits};
    return transformer.run();
}

TransformResult FormatTransform::convertDelimited(const std::string_view text,
                                                   const DelimitedTextOptions& options) {
    DelimitedTransformer transformer{text, options};
    auto result = transformer.run();
    if (result && result.validation.valid) {
        result.validation = delimitedValidation(text, true, text.size(), {});
    }
    return result;
}

}
