#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace nff::search {

enum class SearchKind : std::uint8_t {
    Literal,
    RegularExpression,
};

enum class SearchDirection : std::uint8_t {
    Forward,
    Backward,
};

struct SearchOptions final {
    SearchKind kind{SearchKind::Literal};
    bool caseSensitive{true};
    bool wholeWord{false};
    bool wrapAround{true};
    std::size_t maxResults{100'000};
};

struct SearchMatch final {
    std::uint64_t offset{};
    std::uint64_t length{};

    [[nodiscard]] friend constexpr bool operator==(const SearchMatch&,
                                                   const SearchMatch&) noexcept = default;
};

struct FindResult final {
    std::optional<SearchMatch> match;
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return !error;
    }
};

struct FindAllResult final {
    std::vector<SearchMatch> matches;
    std::error_code error;
    bool truncated{false};

    [[nodiscard]] explicit operator bool() const noexcept {
        return !error;
    }
};

struct ReplaceResult final {
    std::string text;
    std::size_t replacements{};
    std::error_code error;
    bool truncated{false};

    [[nodiscard]] explicit operator bool() const noexcept {
        return !error;
    }
};

struct ReplaceMatchResult final {
    std::string text;
    std::uint64_t replacementLength{};
    std::error_code error;
    bool replaced{false};

    [[nodiscard]] explicit operator bool() const noexcept {
        return !error;
    }
};

class TextSearch final {
public:
    [[nodiscard]] static FindResult find(std::string_view text,
                                         std::string_view pattern,
                                         std::size_t startOffset,
                                         SearchDirection direction,
                                         const SearchOptions& options = {});

    [[nodiscard]] static FindResult findNext(std::string_view text,
                                             std::string_view pattern,
                                             std::size_t startOffset,
                                             const SearchOptions& options = {});

    [[nodiscard]] static FindResult findPrevious(std::string_view text,
                                                 std::string_view pattern,
                                                 std::size_t startOffset,
                                                 const SearchOptions& options = {});

    [[nodiscard]] static FindAllResult findAll(std::string_view text,
                                               std::string_view pattern,
                                               const SearchOptions& options = {});

    [[nodiscard]] static ReplaceMatchResult replaceMatch(
        std::string_view text,
        std::string_view pattern,
        std::string_view replacement,
        SearchMatch match,
        const SearchOptions& options = {});

    [[nodiscard]] static ReplaceResult replaceAll(std::string_view text,
                                                  std::string_view pattern,
                                                  std::string_view replacement,
                                                  const SearchOptions& options = {});
};

}
