#include "notepadFasaFiso/gui/SelectionAppearanceModel.hpp"

#include <set>
#include <tuple>

namespace nff::gui {

namespace {

template <typename T>
class PropertyAccumulator final {
public:
    void observe(const std::optional<T> value) noexcept {
        if (!seen_) {
            seen_ = true;
            value_ = value;
            return;
        }
        if (value_ != value) {
            mixed_ = true;
        }
    }

    [[nodiscard]] SelectionAppearanceProperty<T> summary() const noexcept {
        return {.mixed = mixed_, .value = mixed_ ? std::optional<T>{} : value_};
    }

private:
    bool seen_{false};
    bool mixed_{false};
    std::optional<T> value_{};
};

}

std::optional<SelectionAppearanceSummary> summarizeSelectionAppearance(
    const metadata::TextAppearanceMap& appearance,
    const std::uint64_t begin,
    const std::uint64_t end) noexcept {
    if (begin >= end) {
        return std::nullopt;
    }

    PropertyAccumulator<std::uint32_t> foreground;
    PropertyAccumulator<metadata::FontFamilyId> fontFamily;
    PropertyAccumulator<std::uint8_t> fontSize;
    PropertyAccumulator<bool> spoiler;

    const auto observe = [&](const metadata::AppearanceStyle& style) noexcept {
        foreground.observe(style.foregroundArgb);
        fontFamily.observe(style.fontFamilyId);
        fontSize.observe(style.fontSizePoints);
        spoiler.observe(std::optional<bool>{style.spoiler});
    };
    const metadata::AppearanceStyle inherited{};

    std::uint64_t cursor = begin;
    for (const auto& span : appearance.spans()) {
        if (span.end <= begin) {
            continue;
        }
        if (span.begin >= end) {
            break;
        }
        const auto overlapBegin = std::max(begin, span.begin);
        const auto overlapEnd = std::min(end, span.end);
        if (cursor < overlapBegin) {
            observe(inherited);
        }
        if (overlapBegin < overlapEnd) {
            observe(span.style);
            cursor = overlapEnd;
        }
    }
    if (cursor < end) {
        observe(inherited);
    }

    return SelectionAppearanceSummary{
        .foregroundArgb = foreground.summary(),
        .fontFamilyId = fontFamily.summary(),
        .fontSizePoints = fontSize.summary(),
        .spoiler = spoiler.summary(),
    };
}

std::size_t renderedAppearanceCombinationCount(const metadata::TextAppearanceMap& appearance) {
    using Key = std::tuple<std::optional<std::uint32_t>,
                           std::optional<metadata::FontFamilyId>,
                           std::optional<std::uint8_t>>;
    std::set<Key> combinations;
    for (const auto& span : appearance.spans()) {
        const auto& style = span.style;
        if (!style.foregroundArgb && !style.fontFamilyId && !style.fontSizePoints) {
            continue;
        }
        combinations.emplace(style.foregroundArgb, style.fontFamilyId, style.fontSizePoints);
    }
    return combinations.size();
}

bool appearanceFitsStyleBudget(const metadata::TextAppearanceMap& appearance,
                               const std::size_t maximumStyles) {
    return renderedAppearanceCombinationCount(appearance) <= maximumStyles;
}

std::optional<AppearanceRange> contiguousSpoilerRegionAt(
    const std::span<const metadata::TextAppearanceSpan> spans,
    const std::uint64_t offset) noexcept {
    std::size_t index = 0U;
    while (index < spans.size() && spans[index].end <= offset) {
        ++index;
    }
    if (index >= spans.size() || spans[index].begin > offset || spans[index].end <= offset ||
        !spans[index].style.spoiler) {
        return std::nullopt;
    }

    std::uint64_t begin = spans[index].begin;
    std::uint64_t end = spans[index].end;
    while (index > 0U && spans[index - 1U].style.spoiler && spans[index - 1U].end == begin) {
        --index;
        begin = spans[index].begin;
    }
    for (std::size_t next = index + 1U; next < spans.size(); ++next) {
        if (!spans[next].style.spoiler || spans[next].begin != end) {
            break;
        }
        end = spans[next].end;
    }
    return AppearanceRange{begin, end};
}

}
