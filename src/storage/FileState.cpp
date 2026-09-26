#include "notepadFasaFiso/storage/FileState.hpp"

namespace nff::storage {

FileStateResult FileStateTracker::capture(const std::filesystem::path& path) noexcept {
    if (path.empty()) {
        return {{}, std::make_error_code(std::errc::invalid_argument)};
    }

    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error) {
        if (error == std::errc::no_such_file_or_directory) {
            return {{false, 0, {}}, {}};
        }
        return {{}, error};
    }

    if (!std::filesystem::exists(status)) {
        return {{false, 0, {}}, {}};
    }
    if (!std::filesystem::is_regular_file(status)) {
        return {{}, std::make_error_code(std::errc::invalid_argument)};
    }

    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return {{}, error};
    }

    const auto writeTime = std::filesystem::last_write_time(path, error);
    if (error) {
        return {{}, error};
    }

    FileIdentity identity{};
    try {
        RandomAccessFile file;
        if (!file.open(path)) {
            identity = file.identity();
        }
    } catch (...) {

    }

    return {{true, size, writeTime, identity}, {}};
}

FileChangeState FileStateTracker::compare(const FileState& baseline,
                                          const FileStateResult& current) noexcept {
    if (current.error) {
        return FileChangeState::Inaccessible;
    }
    if (!baseline.exists) {
        return current.state.exists ? FileChangeState::Modified : FileChangeState::Unchanged;
    }
    if (!current.state.exists) {
        return FileChangeState::Deleted;
    }
    if (baseline.identity.valid && current.state.identity.valid &&
        baseline.identity != current.state.identity) {
        return FileChangeState::Modified;
    }
    if (baseline.size != current.state.size || baseline.writeTime != current.state.writeTime) {
        return FileChangeState::Modified;
    }
    return FileChangeState::Unchanged;
}

}
