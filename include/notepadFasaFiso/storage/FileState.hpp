#pragma once

#include "notepadFasaFiso/storage/RandomAccessFile.hpp"

#include <cstdint>
#include <filesystem>
#include <system_error>

namespace nff::storage {

enum class FileChangeState : std::uint8_t {
    Untracked,
    Unchanged,
    Modified,
    Deleted,
    Inaccessible
};

struct FileState {
    bool exists{false};
    std::uintmax_t size{0};
    std::filesystem::file_time_type writeTime{};
    FileIdentity identity{};
};

struct FileStateResult {
    FileState state{};
    std::error_code error{};

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

class FileStateTracker final {
public:
    [[nodiscard]] static FileStateResult capture(const std::filesystem::path& path) noexcept;
    [[nodiscard]] static FileChangeState compare(const FileState& baseline,
                                                 const FileStateResult& current) noexcept;
};

}
