#include "notepadFasaFiso/storage/FileReader.hpp"

#include "notepadFasaFiso/storage/RandomAccessFile.hpp"

#include <algorithm>
#include <limits>

namespace nff::storage {
namespace {

[[nodiscard]] ReadResult readBytes(const std::filesystem::path& path,
                                   const std::size_t requestedBytes) {
    ReadResult result;
    RandomAccessFile file;
    if (const auto error = file.open(path)) {
        result.error = error;
        return result;
    }
    if (requestedBytes == 0U) {
        return result;
    }

    const auto available = static_cast<std::size_t>(
        std::min<std::uint64_t>(file.size(), static_cast<std::uint64_t>(requestedBytes)));
    result.bytes.resize(available);
    const auto read = file.readAt(0U, result.bytes);
    if (!read) {
        result.error = read.error;
        result.bytes.clear();
        return result;
    }
    result.bytes.resize(read.bytesRead);
    return result;
}

}

ReadResult FileReader::readAll(const std::filesystem::path& path, const std::size_t maximumBytes) {
    RandomAccessFile file;
    if (const auto error = file.open(path)) {
        return {{}, error};
    }

    const auto size = file.size();
    if (size > static_cast<std::uint64_t>(maximumBytes) ||
        size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return {{}, std::make_error_code(std::errc::file_too_large)};
    }

    ReadResult result;
    if (size == 0U) {
        return result;
    }
    result.bytes.resize(static_cast<std::size_t>(size));
    const auto read = file.readAt(0U, result.bytes);
    if (!read) {
        result.error = read.error;
        result.bytes.clear();
        return result;
    }
    result.bytes.resize(read.bytesRead);
    return result;
}

ReadResult FileReader::readPrefix(const std::filesystem::path& path,
                                  const std::size_t maximumBytes) {
    return readBytes(path, maximumBytes);
}

}
