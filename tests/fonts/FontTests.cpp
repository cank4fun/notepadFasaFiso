#include "notepadFasaFiso/fonts/FontManager.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

char lower(const char value) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

bool lessInsensitive(const std::string& left, const std::string& right) {
    return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
                                        [](const char a, const char b) { return lower(a) < lower(b); });
}

void testRefresh() {
    nff::fonts::FontManager manager;
    const auto refreshed = manager.refresh();
    if (!refreshed) {
        require(refreshed.error == std::make_error_code(std::errc::function_not_supported),
                "font enumeration only fails when platform backend is unavailable");
        return;
    }

    require(refreshed.familyCount == manager.families().size(), "font count matches snapshot");
    require(std::is_sorted(manager.families().begin(), manager.families().end(), lessInsensitive),
            "font families are sorted");
    if (!manager.families().empty()) {
        require(manager.contains(manager.families().front()), "font lookup finds enumerated family");
    }

    const auto generation = manager.generation();
    const auto second = manager.refresh();
    require(static_cast<bool>(second), "second font refresh succeeds");
    if (!second.changed) {
        require(manager.generation() == generation, "unchanged font list preserves generation");
    }
}

}

int main() {
    testRefresh();
    return EXIT_SUCCESS;
}
