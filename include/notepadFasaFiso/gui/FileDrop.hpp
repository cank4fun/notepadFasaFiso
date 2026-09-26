#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace nff::gui {

struct FileDropSelection final {
    std::vector<std::filesystem::path> files;
    std::size_t ignoredEntries{0U};
};

[[nodiscard]] FileDropSelection prepareFileDropPaths(
    std::span<const std::filesystem::path> candidates);

}
