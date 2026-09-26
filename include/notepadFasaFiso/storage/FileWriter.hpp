#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <system_error>

namespace nff::storage {

class FileWriter final {
public:
    [[nodiscard]] static std::error_code writeAtomically(const std::filesystem::path& path,
                                                         std::span<const std::byte> bytes);
};

}
