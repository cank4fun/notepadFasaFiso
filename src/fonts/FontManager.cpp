#include "notepadFasaFiso/fonts/FontManager.hpp"

#include "notepadFasaFiso/platform/Platform.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <utility>

namespace nff::fonts {
namespace {

[[nodiscard]] char asciiLower(const char value) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

[[nodiscard]] bool equalInsensitive(const std::string_view left,
                                    const std::string_view right) noexcept {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), right.end(),
                      [](const char a, const char b) { return asciiLower(a) == asciiLower(b); });
}

[[nodiscard]] bool lessInsensitive(const std::string& left, const std::string& right) noexcept {
    return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
                                        [](const char a, const char b) {
                                            return asciiLower(a) < asciiLower(b);
                                        });
}

}

FontRefreshResult FontManager::refresh() {
    auto result = platform::systemFontFamilies();
    if (result.error) {
        return {families_.size(), false, result.error};
    }

    std::erase_if(result.families, [](const std::string& family) { return family.empty(); });
    std::ranges::sort(result.families, lessInsensitive);
    result.families.erase(std::unique(result.families.begin(), result.families.end(),
                                      equalInsensitive), result.families.end());

    const bool changed = result.families != families_;
    if (changed) {
        families_ = std::move(result.families);
        ++generation_;
    }
    return {families_.size(), changed, {}};
}

std::span<const std::string> FontManager::families() const noexcept {
    return families_;
}

bool FontManager::contains(const std::string_view family) const noexcept {
    return std::ranges::any_of(families_, [&](const std::string& existing) {
        return equalInsensitive(existing, family);
    });
}

std::uint64_t FontManager::generation() const noexcept {
    return generation_;
}

}
