#include "notepadFasaFiso/storage/AtomicSave.hpp"

#include "notepadFasaFiso/platform/Platform.hpp"

#include <algorithm>
#include <limits>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace nff::storage {
namespace {

[[nodiscard]] std::error_code writeTemporaryExclusive(
    const std::filesystem::path& path,
    const std::span<const std::byte> bytes) noexcept {
#ifdef _WIN32
    HANDLE handle = ::CreateFileW(path.c_str(),
                                  GENERIC_WRITE,
                                  0,
                                  nullptr,
                                  CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return {static_cast<int>(::GetLastError()), std::system_category()};
    }

    std::error_code result;
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (::WriteFile(handle, bytes.data() + offset, chunk, &written, nullptr) == 0 ||
            written == 0U) {
            result = {static_cast<int>(::GetLastError()), std::system_category()};
            if (!result) {
                result = std::make_error_code(std::errc::io_error);
            }
            break;
        }
        offset += static_cast<std::size_t>(written);
    }

    if (!result && ::FlushFileBuffers(handle) == 0) {
        result = {static_cast<int>(::GetLastError()), std::system_category()};
    }
    if (::CloseHandle(handle) == 0 && !result) {
        result = {static_cast<int>(::GetLastError()), std::system_category()};
    }

    if (result) {
        std::error_code cleanup;
        std::filesystem::remove(path, cleanup);
    }
    return result;
#else
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (descriptor < 0) {
        return std::error_code(errno, std::generic_category());
    }

    std::error_code result;
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto chunk = std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
        const auto written = ::write(descriptor, bytes.data() + offset, chunk);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            result = std::error_code(errno, std::generic_category());
            break;
        }
        if (written == 0) {
            result = std::make_error_code(std::errc::io_error);
            break;
        }
        offset += static_cast<std::size_t>(written);
    }

    while (!result && ::fsync(descriptor) != 0) {
        if (errno == EINTR) {
            continue;
        }
        result = std::error_code(errno, std::generic_category());
    }
    if (::close(descriptor) != 0 && !result) {
        result = std::error_code(errno, std::generic_category());
    }

    if (result) {
        std::error_code cleanup;
        std::filesystem::remove(path, cleanup);
    }
    return result;
#endif
}

[[nodiscard]] std::error_code syncParentDirectory(
    const std::filesystem::path& parent) noexcept {
#ifdef _WIN32
    static_cast<void>(parent);
    return {};
#else
    const auto directory = parent.empty() ? std::filesystem::path(".") : parent;
    const int descriptor = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor < 0) {
        return std::error_code(errno, std::generic_category());
    }

    std::error_code result;
    while (::fsync(descriptor) != 0) {
        if (errno == EINTR) {
            continue;
        }
        result = std::error_code(errno, std::generic_category());
        break;
    }
    if (::close(descriptor) != 0 && !result) {
        result = std::error_code(errno, std::generic_category());
    }
    return result;
#endif
}

}

std::error_code AtomicSave::write(const std::filesystem::path& path,
                                  const std::span<const std::byte> bytes) {
    std::error_code error;
    const auto targetStatus = std::filesystem::symlink_status(path, error);
    if (error && error != std::errc::no_such_file_or_directory) {
        return error;
    }
    if (!error && std::filesystem::exists(targetStatus) &&
        std::filesystem::is_symlink(targetStatus)) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    error.clear();

    const auto parent = path.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, error)) {
        if (error) {
            return error;
        }
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }

    const auto temporary = platform::temporarySiblingPath(path);
    if (const auto temporaryError = writeTemporaryExclusive(temporary, bytes)) {
        return temporaryError;
    }

    if (!platform::replaceFile(temporary, path, error)) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return error;
    }

    return syncParentDirectory(parent);
}

}
