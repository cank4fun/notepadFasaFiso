#include "notepadFasaFiso/viewer/TextPresentation.hpp"

#include <algorithm>
#include <utility>

namespace nff::viewer {

void TextPresentationModel::setFilter(std::optional<LineFilter> filter) {
    filter_ = std::move(filter);
}

void TextPresentationModel::clearFilter() noexcept { filter_.reset(); }

const std::optional<LineFilter>& TextPresentationModel::filter() const noexcept { return filter_; }

bool TextPresentationModel::addHighlightRule(HighlightRule rule) {
    if (rule.id == 0U || rule.pattern.empty()) {
        return false;
    }
    const auto duplicate = std::ranges::find_if(highlightRules_, [&](const HighlightRule& current) {
        return current.id == rule.id;
    });
    if (duplicate != highlightRules_.end()) {
        return false;
    }
    highlightRules_.push_back(std::move(rule));
    return true;
}

bool TextPresentationModel::removeHighlightRule(const std::uint64_t id) noexcept {
    const auto before = highlightRules_.size();
    std::erase_if(highlightRules_, [id](const HighlightRule& rule) { return rule.id == id; });
    return highlightRules_.size() != before;
}

void TextPresentationModel::clearHighlightRules() noexcept { highlightRules_.clear(); }

const std::vector<HighlightRule>& TextPresentationModel::highlightRules() const noexcept {
    return highlightRules_;
}

LinePresentation TextPresentationModel::evaluate(const std::string_view line) const {
    LinePresentation result;

    if (filter_.has_value()) {
        const auto matches = search::TextSearch::findAll(line,
                                                         filter_->pattern,
                                                         filter_->searchOptions);
        if (!matches) {
            result.error = matches.error;
            return result;
        }
        const bool matched = !matches.matches.empty();
        result.visible = filter_->mode == LineFilterMode::IncludeMatches ? matched : !matched;
    }

    for (const auto& rule : highlightRules_) {
        const auto matches = search::TextSearch::findAll(line, rule.pattern, rule.searchOptions);
        if (!matches) {
            result.error = matches.error;
            result.highlights.clear();
            return result;
        }
        result.highlights.reserve(result.highlights.size() + matches.matches.size());
        for (const auto& match : matches.matches) {
            result.highlights.push_back({rule.id, match.offset, match.length});
        }
    }

    return result;
}

}
