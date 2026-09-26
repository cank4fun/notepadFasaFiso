#pragma once

#include "notepadFasaFiso/metadata/MetadataStore.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace nff::gui {

template <typename T>
struct SelectionAppearanceProperty final {
    bool mixed{false};
    std::optional<T> value{};
    friend bool operator==(const SelectionAppearanceProperty&,
                           const SelectionAppearanceProperty&) = default;
};

struct SelectionAppearanceSummary final {
    SelectionAppearanceProperty<std::uint32_t> foregroundArgb{};
    SelectionAppearanceProperty<metadata::FontFamilyId> fontFamilyId{};
    SelectionAppearanceProperty<std::uint8_t> fontSizePoints{};
    SelectionAppearanceProperty<bool> spoiler{};
};

struct AppearanceRange final {
    std::uint64_t begin{0U};
    std::uint64_t end{0U};
    friend bool operator==(const AppearanceRange&, const AppearanceRange&) = default;
};

[[nodiscard]] std::optional<SelectionAppearanceSummary> summarizeSelectionAppearance(
    const metadata::TextAppearanceMap& appearance,
    std::uint64_t begin,
    std::uint64_t end) noexcept;

[[nodiscard]] std::size_t renderedAppearanceCombinationCount(
    const metadata::TextAppearanceMap& appearance);
[[nodiscard]] bool appearanceFitsStyleBudget(
    const metadata::TextAppearanceMap& appearance,
    std::size_t maximumStyles = 128U);
[[nodiscard]] std::optional<AppearanceRange> contiguousSpoilerRegionAt(
    std::span<const metadata::TextAppearanceSpan> spans,
    std::uint64_t offset) noexcept;

}
