#include "notepadFasaFiso/platform/Platform.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#ifndef _WIN32
#include <cerrno>
#include <sys/stat.h>
#endif

namespace nff::platform {

std::filesystem::path temporarySiblingPath(const std::filesystem::path& target) {
    static std::atomic<std::uint64_t> sequence{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto id = sequence.fetch_add(1, std::memory_order_relaxed);

    auto filename = target.filename().native();
#ifdef _WIN32
    filename += L".nff-tmp-" + std::to_wstring(now) + L"-" + std::to_wstring(id);
#else
    filename += ".nff-tmp-" + std::to_string(now) + "-" + std::to_string(id);
#endif
    return target.parent_path() / filename;
}

std::error_code ensurePrivateDirectory(const std::filesystem::path& path) noexcept {
    if (path.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) {
        return error;
    }
    if (!std::filesystem::is_directory(path, error)) {
        if (error) {
            return error;
        }
        return std::make_error_code(std::errc::not_a_directory);
    }

#ifndef _WIN32
    if (::chmod(path.c_str(), 0700) != 0) {
        return std::error_code(errno, std::generic_category());
    }
#endif
    return {};
}

std::error_code hardenPrivateFile(const std::filesystem::path& path) noexcept {
    if (path.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error) {
        return error;
    }
    if (!std::filesystem::exists(status)) {
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }
    if (std::filesystem::is_symlink(status)) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    if (!std::filesystem::is_regular_file(status)) {
        return std::make_error_code(std::errc::invalid_argument);
    }

#ifndef _WIN32
    if (::chmod(path.c_str(), 0600) != 0) {
        return std::error_code(errno, std::generic_category());
    }
#endif
    return {};
}

}
