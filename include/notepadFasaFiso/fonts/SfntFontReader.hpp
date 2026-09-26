#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace nff::fonts {

struct FontFileInspection final {
    std::vector<std::string> families;
    std::error_code error{};

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

class SfntFontReader final {
public:
    [[nodiscard]] static FontFileInspection inspect(const std::filesystem::path& path);
};

}
