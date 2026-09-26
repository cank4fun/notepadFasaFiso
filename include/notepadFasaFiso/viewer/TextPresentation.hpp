#pragma once

#include "notepadFasaFiso/search/TextSearch.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace nff::viewer {

enum class LineFilterMode : std::uint8_t {
    IncludeMatches,
    ExcludeMatches,
};

struct LineFilter final {
    std::string pattern;
    search::SearchOptions searchOptions{};
    LineFilterMode mode{LineFilterMode::IncludeMatches};
};

struct HighlightRule final {
    std::uint64_t id{};
    std::string pattern;
    search::SearchOptions searchOptions{};
};

struct HighlightSpan final {
    std::uint64_t ruleId{};
    std::uint64_t offset{};
    std::uint64_t length{};
};

struct LinePresentation final {
    bool visible{true};
    std::vector<HighlightSpan> highlights;
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

class TextPresentationModel final {
public:
    void setFilter(std::optional<LineFilter> filter);
    void clearFilter() noexcept;
    [[nodiscard]] const std::optional<LineFilter>& filter() const noexcept;

    [[nodiscard]] bool addHighlightRule(HighlightRule rule);
    [[nodiscard]] bool removeHighlightRule(std::uint64_t id) noexcept;
    void clearHighlightRules() noexcept;
    [[nodiscard]] const std::vector<HighlightRule>& highlightRules() const noexcept;

    [[nodiscard]] LinePresentation evaluate(std::string_view line) const;

private:
    std::optional<LineFilter> filter_;
    std::vector<HighlightRule> highlightRules_;
};

}
