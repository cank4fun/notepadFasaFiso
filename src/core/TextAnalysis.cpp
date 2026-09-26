#include "notepadFasaFiso/core/TextAnalysis.hpp"

#include <algorithm>

namespace nff::core {
namespace {

class StatisticsBuilder final {
public:
    void push(const unsigned char current, const bool hasNext, const unsigned char next) noexcept {
        if (current == '\r') {
            commitLine();
            if (hasNext && next == '\n') {
                ++crlf_;
                skipNext_ = true;
            } else {
                ++cr_;
            }
            return;
        }

        if (current == '\n') {
            commitLine();
            ++lf_;
            return;
        }

        ++currentLineBytes_;
    }

    [[nodiscard]] bool consumeSkip() noexcept {
        if (!skipNext_) {
            return false;
        }
        skipNext_ = false;
        return true;
    }

    [[nodiscard]] TextStatistics finish(const bool hasContent) noexcept {
        commitFinalLine();

        const auto lineBreaks = lf_ + crlf_ + cr_;
        const auto kinds = static_cast<unsigned int>(lf_ > 0) +
                           static_cast<unsigned int>(crlf_ > 0) +
                           static_cast<unsigned int>(cr_ > 0);

        LineEnding ending = LineEnding::Unknown;
        if (kinds > 1U) {
            ending = LineEnding::Mixed;
        } else if (crlf_ > 0) {
            ending = LineEnding::CRLF;
        } else if (lf_ > 0) {
            ending = LineEnding::LF;
        } else if (cr_ > 0) {
            ending = LineEnding::CR;
        }

        return {ending, hasContent ? lineBreaks + 1 : 0, longestLineBytes_,
                lf_, crlf_, cr_, hasContent ? longestLineOccurrences_ : 0U};
    }

private:
    void commitCurrentLine() noexcept {
        if (currentLineBytes_ > longestLineBytes_) {
            longestLineBytes_ = currentLineBytes_;
            longestLineOccurrences_ = 1U;
        } else if (currentLineBytes_ == longestLineBytes_) {
            ++longestLineOccurrences_;
        }
        currentLineBytes_ = 0;
    }

    void commitLine() noexcept {
        commitCurrentLine();
    }

    void commitFinalLine() noexcept {
        commitCurrentLine();
    }

    std::size_t lf_{0};
    std::size_t crlf_{0};
    std::size_t cr_{0};
    std::size_t currentLineBytes_{0};
    std::size_t longestLineBytes_{0};
    std::size_t longestLineOccurrences_{0};
    bool skipNext_{false};
};

template <typename ByteAt>
[[nodiscard]] TextStatistics analyze(const std::size_t size, ByteAt byteAt) noexcept {
    StatisticsBuilder builder;

    for (std::size_t index = 0; index < size; ++index) {
        if (builder.consumeSkip()) {
            continue;
        }

        const auto current = byteAt(index);
        const bool hasNext = index + 1 < size;
        const auto next = hasNext ? byteAt(index + 1) : static_cast<unsigned char>(0);
        builder.push(current, hasNext, next);
    }

    return builder.finish(size != 0);
}

}

TextStatistics TextAnalysis::analyzeUtf8(const std::string_view text) noexcept {
    return analyze(text.size(), [text](const std::size_t index) {
        return static_cast<unsigned char>(text[index]);
    });
}

TextStatistics TextAnalysis::analyzeAsciiCompatible(
    const std::span<const std::byte> bytes) noexcept {
    return analyze(bytes.size(), [bytes](const std::size_t index) {
        return std::to_integer<unsigned char>(bytes[index]);
    });
}

}
