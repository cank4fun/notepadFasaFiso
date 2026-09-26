#include "notepadFasaFiso/viewer/HexPreview.hpp"

#include <algorithm>
#include <limits>

namespace nff::viewer {

std::span<const std::byte> HexWindow::row(const std::uint64_t relativeRow) const noexcept {
    if (bytesPerRow == 0U || relativeRow >= rowCount) {
        return {};
    }
    if (relativeRow > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) /
                          bytesPerRow) {
        return {};
    }

    const auto start = static_cast<std::size_t>(relativeRow) * bytesPerRow;
    if (start >= bytes.size()) {
        return {};
    }
    const auto count = std::min(bytesPerRow, bytes.size() - start);
    return std::span<const std::byte>{bytes}.subspan(start, count);
}

std::error_code HexPreview::open(const std::filesystem::path& path) {
    return file_.open(path);
}

void HexPreview::close() noexcept {
    file_.close();
}

bool HexPreview::isOpen() const noexcept {
    return file_.isOpen();
}

std::uint64_t HexPreview::size() const noexcept {
    return file_.size();
}

std::uint64_t HexPreview::rowCount(const std::size_t bytesPerRow) const noexcept {
    if (bytesPerRow == 0U || file_.size() == 0U) {
        return 0U;
    }
    const auto width = static_cast<std::uint64_t>(bytesPerRow);
    return 1U + ((file_.size() - 1U) / width);
}

const std::filesystem::path& HexPreview::path() const noexcept {
    return file_.path();
}

HexWindow HexPreview::readRows(const std::uint64_t firstRow,
                               const std::uint64_t requestedRows,
                               const std::size_t bytesPerRow,
                               const std::size_t maximumBytes) const {
    HexWindow result;
    result.bytesPerRow = bytesPerRow;
    result.firstRow = firstRow;

    if (!file_.isOpen()) {
        result.error = std::make_error_code(std::errc::bad_file_descriptor);
        return result;
    }
    if (bytesPerRow == 0U || maximumBytes == 0U) {
        result.error = std::make_error_code(std::errc::invalid_argument);
        return result;
    }
    if (bytesPerRow > maximumBytes) {
        result.error = std::make_error_code(std::errc::value_too_large);
        return result;
    }

    const auto width = static_cast<std::uint64_t>(bytesPerRow);
    if (firstRow > std::numeric_limits<std::uint64_t>::max() / width) {
        result.error = std::make_error_code(std::errc::value_too_large);
        return result;
    }

    result.startOffset = firstRow * width;
    if (result.startOffset >= file_.size() || requestedRows == 0U) {
        return result;
    }

    const auto rowsByBudget = static_cast<std::uint64_t>(maximumBytes / bytesPerRow);
    const auto rows = std::min(requestedRows, rowsByBudget);
    if (rows == 0U) {
        result.error = std::make_error_code(std::errc::value_too_large);
        return result;
    }

    const auto remainingBytes = file_.size() - result.startOffset;
    const auto requestedBytes64 = rows > std::numeric_limits<std::uint64_t>::max() / width
                                      ? std::numeric_limits<std::uint64_t>::max()
                                      : rows * width;
    const auto bytesToRead64 = std::min(remainingBytes, requestedBytes64);
    if (bytesToRead64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        result.error = std::make_error_code(std::errc::value_too_large);
        return result;
    }

    const auto bytesToRead = static_cast<std::size_t>(bytesToRead64);
    result.bytes.resize(bytesToRead);
    const auto read = file_.readAt(result.startOffset, result.bytes);
    if (!read) {
        result.bytes.clear();
        result.error = read.error;
        return result;
    }
    result.bytes.resize(read.bytesRead);
    if (!result.bytes.empty()) {
        result.rowCount = 1U + ((static_cast<std::uint64_t>(result.bytes.size()) - 1U) / width);
    }
    return result;
}

char HexPreview::printableAscii(const std::byte value) noexcept {
    const auto byte = std::to_integer<unsigned int>(value);
    return byte >= 0x20U && byte <= 0x7EU ? static_cast<char>(byte) : '.';
}

}
