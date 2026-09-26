#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <system_error>
#include <vector>

namespace nff::storage {

struct ReadResult {
    std::vector<std::byte> bytes;
    std::error_code error{};

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

class FileReader final {
public:
    [[nodiscard]] static ReadResult readAll(const std::filesystem::path& path,
                                            std::size_t maximumBytes);
    [[nodiscard]] static ReadResult readPrefix(const std::filesystem::path& path,
                                               std::size_t maximumBytes);
};

}
