#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace nff::core {

enum class LineSortDirection : std::uint8_t {
    Ascending,
    Descending,
};

struct DuplicateLineOptions final {
    bool caseSensitive{true};
    bool keepEmptyLines{true};
};

class TextUtilities final {
public:
    [[nodiscard]] static std::string trimTrailingWhitespace(std::string_view text);
    [[nodiscard]] static std::string removeEmptyLines(std::string_view text,
                                                      bool whitespaceOnly = true);
    [[nodiscard]] static std::string removeDuplicateLines(
        std::string_view text,
        const DuplicateLineOptions& options = {});
    [[nodiscard]] static std::string sortLines(std::string_view text,
                                               LineSortDirection direction =
                                                   LineSortDirection::Ascending,
                                               bool caseSensitive = true);
    [[nodiscard]] static std::string reverseLines(std::string_view text);

    [[nodiscard]] static std::string expandTabs(std::string_view text,
                                                std::size_t tabWidth = 4U);
    [[nodiscard]] static std::string compressLeadingSpacesToTabs(
        std::string_view text,
        std::size_t tabWidth = 4U);

    [[nodiscard]] static std::string asciiToLower(std::string_view text);
    [[nodiscard]] static std::string asciiToUpper(std::string_view text);
};

}
