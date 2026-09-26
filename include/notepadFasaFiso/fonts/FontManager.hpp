#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace nff::fonts {

struct FontRefreshResult final {
    std::size_t familyCount{0U};
    bool changed{false};
    std::error_code error{};

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

class FontManager final {
public:
    [[nodiscard]] FontRefreshResult refresh();
    [[nodiscard]] std::span<const std::string> families() const noexcept;
    [[nodiscard]] bool contains(std::string_view family) const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;

private:
    std::vector<std::string> families_;
    std::uint64_t generation_{0U};
};

}
