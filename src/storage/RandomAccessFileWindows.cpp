#include "notepadFasaFiso/storage/RandomAccessFile.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <limits>
#include <mutex>

namespace nff::storage {

struct RandomAccessFile::Impl final {
    HANDLE handle{INVALID_HANDLE_VALUE};
    mutable std::mutex mutex;

    ~Impl() {
        if (handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle);
        }
    }
};

RandomAccessFile::RandomAccessFile() : impl_(std::make_unique<Impl>()) {}
RandomAccessFile::~RandomAccessFile() { close(); }
RandomAccessFile::RandomAccessFile(RandomAccessFile&&) noexcept = default;
RandomAccessFile& RandomAccessFile::operator=(RandomAccessFile&&) noexcept = default;

std::error_code RandomAccessFile::open(const std::filesystem::path& path) {
    close();

    const HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return {static_cast<int>(::GetLastError()), std::system_category()};
    }

    LARGE_INTEGER size{};
    if (::GetFileSizeEx(handle, &size) == 0 || size.QuadPart < 0) {
        const auto code = ::GetLastError();
        ::CloseHandle(handle);
        return {static_cast<int>(code), std::system_category()};
    }

    BY_HANDLE_FILE_INFORMATION info{};
    if (::GetFileInformationByHandle(handle, &info) == 0) {
        const auto code = ::GetLastError();
        ::CloseHandle(handle);
        return {static_cast<int>(code), std::system_category()};
    }

    impl_->handle = handle;
    path_ = path;
    size_ = static_cast<std::uint64_t>(size.QuadPart);
    identity_ = {static_cast<std::uint64_t>(info.dwVolumeSerialNumber),
                 static_cast<std::uint64_t>(info.nFileIndexHigh),
                 static_cast<std::uint64_t>(info.nFileIndexLow),
                 true};
    return {};
}

void RandomAccessFile::close() noexcept {
    if (impl_ != nullptr && impl_->handle != INVALID_HANDLE_VALUE) {
        ::CloseHandle(impl_->handle);
        impl_->handle = INVALID_HANDLE_VALUE;
    }
    path_.clear();
    size_ = 0U;
    identity_ = {};
}

bool RandomAccessFile::isOpen() const noexcept {
    return impl_ != nullptr && impl_->handle != INVALID_HANDLE_VALUE;
}

std::uint64_t RandomAccessFile::size() const noexcept { return size_; }
const std::filesystem::path& RandomAccessFile::path() const noexcept { return path_; }
FileIdentity RandomAccessFile::identity() const noexcept { return identity_; }

RandomReadResult RandomAccessFile::readAt(const std::uint64_t offset,
                                          const std::span<std::byte> destination) const {
    if (!isOpen()) {
        return {0U, std::make_error_code(std::errc::bad_file_descriptor)};
    }
    if (offset > size_) {
        return {0U, std::make_error_code(std::errc::invalid_argument)};
    }
    if (destination.empty() || offset == size_) {
        return {};
    }
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return {0U, std::make_error_code(std::errc::value_too_large)};
    }

    std::lock_guard lock(impl_->mutex);
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (::SetFilePointerEx(impl_->handle, position, nullptr, FILE_BEGIN) == 0) {
        return {0U, std::error_code(static_cast<int>(::GetLastError()), std::system_category())};
    }

    const auto available = size_ - offset;
    auto remaining = static_cast<std::size_t>(
        std::min<std::uint64_t>(available, static_cast<std::uint64_t>(destination.size())));
    std::size_t completed = 0U;

    while (remaining > 0U) {
        const auto request = static_cast<DWORD>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD read = 0U;
        if (::ReadFile(impl_->handle, destination.data() + completed, request, &read, nullptr) == 0) {
            return {completed,
                    std::error_code(static_cast<int>(::GetLastError()), std::system_category())};
        }
        if (read == 0U) {
            break;
        }
        completed += static_cast<std::size_t>(read);
        remaining -= static_cast<std::size_t>(read);
    }

    return {completed, {}};
}

}
