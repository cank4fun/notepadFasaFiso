#include "notepadFasaFiso/storage/RandomAccessFile.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace nff::storage {

struct RandomAccessFile::Impl final {
    int descriptor{-1};

    ~Impl() {
        if (descriptor >= 0) {
            ::close(descriptor);
        }
    }
};

RandomAccessFile::RandomAccessFile() : impl_(std::make_unique<Impl>()) {}
RandomAccessFile::~RandomAccessFile() { close(); }
RandomAccessFile::RandomAccessFile(RandomAccessFile&&) noexcept = default;
RandomAccessFile& RandomAccessFile::operator=(RandomAccessFile&&) noexcept = default;

std::error_code RandomAccessFile::open(const std::filesystem::path& path) {
    close();

    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return {errno, std::generic_category()};
    }

    struct stat info {};
    if (::fstat(descriptor, &info) != 0) {
        const std::error_code error(errno, std::generic_category());
        ::close(descriptor);
        return error;
    }
    if (info.st_size < 0) {
        ::close(descriptor);
        return std::make_error_code(std::errc::value_too_large);
    }

    impl_->descriptor = descriptor;
    path_ = path;
    size_ = static_cast<std::uint64_t>(info.st_size);
    identity_ = {static_cast<std::uint64_t>(info.st_dev),
                 static_cast<std::uint64_t>(info.st_ino),
                 0U,
                 true};
    return {};
}

void RandomAccessFile::close() noexcept {
    if (impl_ != nullptr && impl_->descriptor >= 0) {
        ::close(impl_->descriptor);
        impl_->descriptor = -1;
    }
    path_.clear();
    size_ = 0U;
    identity_ = {};
}

bool RandomAccessFile::isOpen() const noexcept {
    return impl_ != nullptr && impl_->descriptor >= 0;
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
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        return {0U, std::make_error_code(std::errc::value_too_large)};
    }

    const auto available = size_ - offset;
    auto remaining = static_cast<std::size_t>(
        std::min<std::uint64_t>(available, static_cast<std::uint64_t>(destination.size())));
    std::size_t completed = 0U;

    while (remaining > 0U) {
        const auto request = std::min<std::size_t>(remaining,
                                                  static_cast<std::size_t>(SSIZE_MAX));
        const auto absolute = offset + static_cast<std::uint64_t>(completed);
        if (absolute > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
            return {completed, std::make_error_code(std::errc::value_too_large)};
        }

        const auto count = ::pread(impl_->descriptor,
                                   destination.data() + completed,
                                   request,
                                   static_cast<off_t>(absolute));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return {completed, std::error_code(errno, std::generic_category())};
        }
        if (count == 0) {
            break;
        }

        const auto advanced = static_cast<std::size_t>(count);
        completed += advanced;
        remaining -= advanced;
    }

    return {completed, {}};
}

}
