#include "notepadFasaFiso/core/Document.hpp"

#include "notepadFasaFiso/core/TextAnalysis.hpp"
#include "notepadFasaFiso/core/TextTransform.hpp"
#include "notepadFasaFiso/encoding/TextCodec.hpp"
#include "notepadFasaFiso/formats/FormatDetector.hpp"
#include "notepadFasaFiso/storage/FileReader.hpp"
#include "notepadFasaFiso/storage/FileWriter.hpp"

#include <cstring>
#include <limits>
#include <utility>

namespace nff::core {
namespace {

[[nodiscard]] bool validUtf8(std::string_view text) noexcept {
    std::size_t index = 0U;
    constexpr std::uint64_t highBits = 0x8080808080808080ULL;

    while (index < text.size()) {
        const auto lead = static_cast<unsigned char>(text[index]);
        if (lead <= 0x7FU) {
            ++index;

            // linus torvalds type shi'
            while (text.size() - index >= sizeof(std::uint64_t)) {
                std::uint64_t word;
                std::memcpy(&word, text.data() + index, sizeof(word));
                if ((word & highBits) != 0U) break;
                index += sizeof(word);
            }
            continue;
        }

        std::size_t width = 0U;
        std::uint32_t minimum = 0U;
        std::uint32_t value = 0U;
        if ((lead & 0xE0U) == 0xC0U) {
            width = 2U;
            minimum = 0x80U;
            value = lead & 0x1FU;
        } else if ((lead & 0xF0U) == 0xE0U) {
            width = 3U;
            minimum = 0x800U;
            value = lead & 0x0FU;
        } else if ((lead & 0xF8U) == 0xF0U) {
            width = 4U;
            minimum = 0x10000U;
            value = lead & 0x07U;
        } else {
            return false;
        }

        if (text.size() - index < width) {
            return false;
        }
        for (std::size_t offset = 1U; offset < width; ++offset) {
            const auto continuation = static_cast<unsigned char>(text[index + offset]);
            if ((continuation & 0xC0U) != 0x80U) {
                return false;
            }
            value = (value << 6U) | (continuation & 0x3FU);
        }

        if (value < minimum || value > 0x10FFFFU ||
            (value >= 0xD800U && value <= 0xDFFFU)) {
            return false;
        }
        index += width;
    }
    return true;
}

[[nodiscard]] bool utf8Boundary(const std::string_view text, const std::size_t offset) noexcept {
    if (offset == 0U || offset == text.size()) {
        return true;
    }
    if (offset > text.size()) {
        return false;
    }
    const auto value = static_cast<unsigned char>(text[offset]);
    return (value & 0xC0U) != 0x80U;
}

[[nodiscard]] std::size_t lineStartAt(const std::string_view text,
                                      const std::size_t offset) noexcept {
    std::size_t position = std::min(offset, text.size());
    if (position != 0U && position < text.size() && text[position] == '\n' &&
        text[position - 1U] == '\r') {
        --position;
    }
    while (position != 0U) {
        const auto index = position - 1U;
        if (text[index] == '\n') {
            return index + 1U;
        }
        if (text[index] == '\r') {
            if (index + 1U < text.size() && text[index + 1U] == '\n' &&
                index + 1U >= offset) {
                position = index;
                continue;
            }
            return index + 1U;
        }
        position = index;
    }
    return 0U;
}

[[nodiscard]] std::size_t lineWindowStart(const std::string_view text,
                                          const std::size_t offset) noexcept {
    const auto currentStart = lineStartAt(text, offset);
    return currentStart == 0U ? 0U : lineStartAt(text, currentStart - 1U);
}

[[nodiscard]] std::size_t lineEndAt(const std::string_view text,
                                    const std::size_t offset) noexcept {
    std::size_t position = std::min(offset, text.size());
    while (position < text.size()) {
        if (text[position] == '\r') {
            ++position;
            if (position < text.size() && text[position] == '\n') {
                ++position;
            }
            return position;
        }
        if (text[position] == '\n') {
            return position + 1U;
        }
        ++position;
    }
    return text.size();
}

[[nodiscard]] std::size_t lineWindowEnd(const std::string_view text,
                                        const std::size_t offset) noexcept {
    const auto currentEnd = lineEndAt(text, offset);
    return currentEnd >= text.size() ? text.size() : lineEndAt(text, currentEnd);
}

[[nodiscard]] LineEnding lineEndingFromCounts(const TextStatistics& statistics) noexcept {
    const auto kinds = static_cast<unsigned int>(statistics.lfCount != 0U) +
                       static_cast<unsigned int>(statistics.crlfCount != 0U) +
                       static_cast<unsigned int>(statistics.crCount != 0U);
    if (kinds > 1U) return LineEnding::Mixed;
    if (statistics.crlfCount != 0U) return LineEnding::CRLF;
    if (statistics.lfCount != 0U) return LineEnding::LF;
    if (statistics.crCount != 0U) return LineEnding::CR;
    return LineEnding::Unknown;
}

[[nodiscard]] constexpr bool supportsBom(const encoding::Encoding value) noexcept {
    switch (value) {
    case encoding::Encoding::Utf8:
    case encoding::Encoding::Utf16LE:
    case encoding::Encoding::Utf16BE:
    case encoding::Encoding::Utf32LE:
    case encoding::Encoding::Utf32BE:
        return true;
    default:
        return false;
    }
}

}

std::error_code Document::load(const std::filesystem::path& path,
                               const std::size_t maximumBytes) {
    const auto inspection = FileSniffer::inspect(path);
    if (!inspection) {
        return inspection.error;
    }
    return loadInspected(path, inspection.profile, maximumBytes);
}

std::error_code Document::loadInspected(const std::filesystem::path& path,
                                        const DocumentProfile& profile,
                                        const std::size_t maximumBytes) {
    if (profile.recommendedMode == OpenMode::Viewer) {
        return std::make_error_code(std::errc::file_too_large);
    }
    if (profile.recommendedMode == OpenMode::BinaryPreview) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    if (profile.encoding.encoding == encoding::Encoding::Unknown8Bit) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    const auto detectedEncoding = profile.encoding.encoding;
    const auto storageEncoding = detectedEncoding == encoding::Encoding::Ascii
                                     ? encoding::Encoding::Utf8
                                     : detectedEncoding;
    return loadDecoded(path, profile, storageEncoding, profile.encoding.hasBom, maximumBytes);
}

void Document::prepareNewFile(const std::filesystem::path& path) {
    path_ = path;
    text_.clear();
    profile_ = {};
    profile_.path = path_;
    saveEncoding_ = encoding::Encoding::Utf8;
    writesBom_ = false;
    modified_ = false;
    textBufferLoaded_ = true;
    revision_ = 0U;
    diskState_ = {};
    tracksDiskState_ = false;
    requiresExplicitOverwrite_ = false;
    refreshTextProfile(0U);
}

void Document::attachReference(const std::filesystem::path& path,
                               const DocumentProfile& profile) {
    path_ = path;
    text_.clear();
    profile_ = profile;
    textStatistics_ = {};
    saveEncoding_ = profile.encoding.encoding == encoding::Encoding::Ascii
                        ? encoding::Encoding::Utf8
                        : profile.encoding.encoding;
    writesBom_ = profile.encoding.hasBom;
    modified_ = false;
    textBufferLoaded_ = false;
    revision_ = 0U;
    requiresExplicitOverwrite_ = false;

    const auto diskState = storage::FileStateTracker::capture(path_);
    if (diskState) {
        diskState_ = diskState.state;
        tracksDiskState_ = diskState.state.exists;
    } else {
        diskState_ = {};
        tracksDiskState_ = false;
    }
}

std::error_code Document::loadAs(const std::filesystem::path& path,
                                 const encoding::Encoding sourceEncoding,
                                 const bool hasBom,
                                 const std::size_t maximumBytes) {
    auto inspection = FileSniffer::inspect(path);
    if (!inspection) {
        return inspection.error;
    }

    if (inspection.profile.fileSize > static_cast<std::uintmax_t>(maximumBytes)) {
        return std::make_error_code(std::errc::file_too_large);
    }

    const bool normalizedBom = hasBom && supportsBom(sourceEncoding);
    inspection.profile.encoding = {sourceEncoding, normalizedBom,
                                   sourceEncoding == encoding::Encoding::Utf8, false};
    inspection.profile.recommendedMode = OpenMode::Editor;
    return loadDecoded(path, inspection.profile, sourceEncoding, normalizedBom, maximumBytes);
}

std::error_code Document::loadDecoded(const std::filesystem::path& path,
                                      const DocumentProfile& profile,
                                      const encoding::Encoding sourceEncoding,
                                      const bool hasBom,
                                      const std::size_t maximumBytes) {
    auto read = storage::FileReader::readAll(path, maximumBytes);
    if (!read) {
        return read.error;
    }

    auto decoded = encoding::TextCodec::decode(read.bytes, sourceEncoding, hasBom);
    if (!decoded) {
        return decoded.error;
    }

    path_ = path;
    text_ = std::move(decoded.text);
    profile_ = profile;
    saveEncoding_ = sourceEncoding == encoding::Encoding::Ascii ? encoding::Encoding::Utf8
                                                                  : sourceEncoding;
    writesBom_ = hasBom && supportsBom(sourceEncoding);
    modified_ = false;
    textBufferLoaded_ = true;
    revision_ = 0;
    requiresExplicitOverwrite_ = false;

    const auto diskState = storage::FileStateTracker::capture(path_);
    if (diskState) {
        diskState_ = diskState.state;
        tracksDiskState_ = true;
    } else {
        diskState_ = {};
        tracksDiskState_ = false;
    }

    setTextStatistics(TextAnalysis::analyzeUtf8(text_));
    return {};
}

std::error_code Document::save() {
    return save(SaveOptions{});
}

std::error_code Document::save(const SaveOptions& options) {
    if (path_.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    return writeTo(path_, options, false, true, true);
}

std::error_code Document::saveAs(const std::filesystem::path& path) {
    return saveAs(path, SaveOptions{});
}

std::error_code Document::saveAs(const std::filesystem::path& path,
                                 const SaveOptions& options) {
    if (path.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    return writeTo(path, options, true, true, false);
}

std::error_code Document::saveCopy(const std::filesystem::path& path) {
    return saveCopy(path, SaveOptions{});
}

std::error_code Document::saveCopy(const std::filesystem::path& path,
                                   const SaveOptions& options) {
    if (path.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    return writeTo(path, options, false, false, false);
}

std::error_code Document::writeTo(const std::filesystem::path& path,
                                  const SaveOptions& options,
                                  const bool adoptPath,
                                  const bool commitDocumentState,
                                  const bool checkExternalState) {
    if (!textBufferLoaded_) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    if (checkExternalState && requiresExplicitOverwrite_ && !options.allowExternalOverwrite) {
        return std::make_error_code(std::errc::text_file_busy);
    }

    if (checkExternalState && tracksDiskState_ && !options.allowExternalOverwrite) {
        const auto currentState = storage::FileStateTracker::capture(path_);
        const auto changeState = storage::FileStateTracker::compare(diskState_, currentState);
        if (changeState == storage::FileChangeState::Modified) {
            return std::make_error_code(std::errc::text_file_busy);
        }
        if (changeState == storage::FileChangeState::Deleted) {
            return std::make_error_code(std::errc::no_such_file_or_directory);
        }
        if (changeState == storage::FileChangeState::Inaccessible) {
            return currentState.error ? currentState.error : std::make_error_code(std::errc::io_error);
        }
    }

    const auto targetEncoding = options.encoding.value_or(saveEncoding_);
    const auto targetBom = options.writeBom.value_or(writesBom_);
    if (targetBom && !supportsBom(targetEncoding)) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    const auto outputText = TextTransform::normalizeLineEndings(text_, options.lineEnding);
    const auto encoded = encoding::TextCodec::encode(outputText, targetEncoding, targetBom);
    if (!encoded) {
        return encoded.error;
    }

    const auto error = storage::FileWriter::writeAtomically(path, encoded.bytes);
    if (error) {
        return error;
    }

    if (!commitDocumentState) {
        return {};
    }

    if (adoptPath) {
        path_ = path;
        profile_.path = path;
    }
    text_ = outputText;
    saveEncoding_ = targetEncoding;
    writesBom_ = targetBom;
    refreshTextProfile(encoded.bytes.size());
    modified_ = false;
    requiresExplicitOverwrite_ = false;

    const auto diskState = storage::FileStateTracker::capture(path_);
    if (diskState) {
        diskState_ = diskState.state;
        tracksDiskState_ = true;
    } else {
        diskState_ = {};
        tracksDiskState_ = false;
    }
    return {};
}

void Document::refreshTextProfile(const std::size_t encodedSize) noexcept {
    profile_.fileSize = encodedSize;
    profile_.encoding = {saveEncoding_, writesBom_,
                         saveEncoding_ == encoding::Encoding::Utf8 ||
                             saveEncoding_ == encoding::Encoding::Ascii,
                         false};
    profile_.sampledEntireFile = true;
    profile_.recommendedMode = OpenMode::Editor;

    setTextStatistics(TextAnalysis::analyzeUtf8(text_));
    profile_.format = formats::FormatDetector::detect(path_, text_, true);
}

void Document::setTextStatistics(const TextStatistics& statistics) noexcept {
    textStatistics_ = statistics;
    profile_.lineEnding = statistics.lineEnding;
    profile_.longestSampledLine = statistics.longestLineBytes;
}

void Document::updateTextStatisticsForEdit(const TextStatistics& oldWindowStatistics,
                                           const std::size_t windowStart,
                                           const std::size_t oldWindowEnd,
                                           const std::size_t erasedBytes,
                                           const std::size_t insertedBytes) {
    const auto subtract = [](const std::size_t total, const std::size_t part) noexcept {
        return total >= part ? total - part : 0U;
    };
    const auto mappedEndBase = oldWindowEnd >= erasedBytes ? oldWindowEnd - erasedBytes : 0U;
    const auto mappedEnd = std::min(text_.size(), mappedEndBase + insertedBytes);
    const auto newWindowStatistics = TextAnalysis::analyzeUtf8(
        std::string_view{text_}.substr(windowStart, mappedEnd - windowStart));

    TextStatistics next = textStatistics_;
    next.lfCount = subtract(next.lfCount, oldWindowStatistics.lfCount) +
                   newWindowStatistics.lfCount;
    next.crlfCount = subtract(next.crlfCount, oldWindowStatistics.crlfCount) +
                     newWindowStatistics.crlfCount;
    next.crCount = subtract(next.crCount, oldWindowStatistics.crCount) +
                   newWindowStatistics.crCount;
    next.lineEnding = lineEndingFromCounts(next);
    const auto breaks = next.lfCount + next.crlfCount + next.crCount;
    next.lineCount = text_.empty() ? 0U : breaks + 1U;

    const auto oldGlobalLongest = textStatistics_.longestLineBytes;
    if (newWindowStatistics.longestLineBytes > oldGlobalLongest) {
        next.longestLineBytes = newWindowStatistics.longestLineBytes;
        next.longestLineOccurrences = newWindowStatistics.longestLineOccurrences;
    } else if (oldWindowStatistics.longestLineBytes < oldGlobalLongest) {
        next.longestLineBytes = oldGlobalLongest;
        next.longestLineOccurrences = textStatistics_.longestLineOccurrences;
    } else {
        const auto outsideOccurrences = subtract(textStatistics_.longestLineOccurrences,
                                                 oldWindowStatistics.longestLineOccurrences);
        if (newWindowStatistics.longestLineBytes == oldGlobalLongest) {
            next.longestLineBytes = oldGlobalLongest;
            next.longestLineOccurrences = outsideOccurrences +
                                          newWindowStatistics.longestLineOccurrences;
        } else if (outsideOccurrences != 0U) {
            next.longestLineBytes = oldGlobalLongest;
            next.longestLineOccurrences = outsideOccurrences;
        } else {
            setTextStatistics(TextAnalysis::analyzeUtf8(text_));
            return;
        }
    }
    setTextStatistics(next);
}

const std::filesystem::path& Document::path() const noexcept {
    return path_;
}

std::string_view Document::text() const noexcept {
    return text_;
}

const DocumentProfile& Document::profile() const noexcept {
    return profile_;
}

const storage::FileState* Document::trackedFileState() const noexcept {
    return tracksDiskState_ ? &diskState_ : nullptr;
}

encoding::Encoding Document::saveEncoding() const noexcept {
    return saveEncoding_;
}

bool Document::writesBom() const noexcept {
    return writesBom_;
}

std::error_code Document::setSaveEncoding(const encoding::Encoding encoding) {
    if (!textBufferLoaded_) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    if (encoding == encoding::Encoding::Unknown8Bit ||
        encoding == encoding::Encoding::Binary) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    const bool nextBom = writesBom_ && supportsBom(encoding);
    if (saveEncoding_ == encoding && writesBom_ == nextBom) {
        return {};
    }
    saveEncoding_ = encoding;
    writesBom_ = nextBom;
    modified_ = true;
    return {};
}

std::error_code Document::setWritesBom(const bool writeBom) {
    if (!textBufferLoaded_) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    if (writeBom && !supportsBom(saveEncoding_)) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    if (writesBom_ == writeBom) {
        return {};
    }
    writesBom_ = writeBom;
    modified_ = true;
    return {};
}

bool Document::modified() const noexcept {
    return modified_;
}

bool Document::textBufferLoaded() const noexcept {
    return textBufferLoaded_;
}

std::uint64_t Document::revision() const noexcept {
    return revision_;
}

bool Document::requiresExplicitOverwrite() const noexcept {
    return requiresExplicitOverwrite_;
}

std::error_code Document::restoreRecovered(std::filesystem::path path,
                                           std::string text,
                                           const encoding::Encoding saveEncoding,
                                           const bool writeBom) {
    if (saveEncoding == encoding::Encoding::Binary ||
        saveEncoding == encoding::Encoding::Unknown8Bit ||
        (writeBom && !supportsBom(saveEncoding))) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    path_ = std::move(path);
    text_ = std::move(text);
    saveEncoding_ = saveEncoding == encoding::Encoding::Ascii ? encoding::Encoding::Utf8
                                                               : saveEncoding;
    writesBom_ = writeBom;
    modified_ = true;
    textBufferLoaded_ = true;
    ++revision_;
    requiresExplicitOverwrite_ = !path_.empty();

    std::size_t profileBytes = text_.size();
    if (!path_.empty()) {
        const auto diskState = storage::FileStateTracker::capture(path_);
        if (diskState) {
            diskState_ = diskState.state;
            tracksDiskState_ = diskState.state.exists;
            if (diskState.state.exists &&
                diskState.state.size <= static_cast<std::uintmax_t>(
                    std::numeric_limits<std::size_t>::max())) {
                profileBytes = static_cast<std::size_t>(diskState.state.size);
            }
        } else {
            diskState_ = {};
            tracksDiskState_ = false;
        }
    } else {
        diskState_ = {};
        tracksDiskState_ = false;
    }

    profile_.path = path_;
    refreshTextProfile(profileBytes);
    return {};
}

storage::FileChangeState Document::externalChangeState() const noexcept {
    if (!tracksDiskState_ || path_.empty()) {
        return storage::FileChangeState::Untracked;
    }
    return storage::FileStateTracker::compare(diskState_, storage::FileStateTracker::capture(path_));
}

std::error_code Document::applyEdit(const std::size_t offset,
                                    const std::size_t eraseBytes,
                                    const std::string_view insertedText) {
    if (!textBufferLoaded_) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    if (offset > text_.size() || eraseBytes > text_.size() - offset) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    const auto end = offset + eraseBytes;
    if (!utf8Boundary(text_, offset) || !utf8Boundary(text_, end) || !validUtf8(insertedText)) {
        return std::make_error_code(std::errc::illegal_byte_sequence);
    }

    const auto before = std::string_view{text_};
    const auto windowStart = lineWindowStart(before, offset);
    const auto oldWindowEnd = lineWindowEnd(before, offset + eraseBytes);
    const auto oldWindowStatistics = TextAnalysis::analyzeUtf8(
        before.substr(windowStart, oldWindowEnd - windowStart));

    text_.replace(offset, eraseBytes, insertedText);
    updateTextStatisticsForEdit(oldWindowStatistics, windowStart, oldWindowEnd,
                                eraseBytes, insertedText.size());
    modified_ = true;
    ++revision_;
    return {};
}

void Document::replaceText(std::string text) {
    if (!textBufferLoaded_) {
        saveEncoding_ = encoding::Encoding::Utf8;
        writesBom_ = false;
        profile_.encoding = {encoding::Encoding::Utf8, false, true, false};
        profile_.recommendedMode = OpenMode::Editor;
    }
    textBufferLoaded_ = true;
    text_ = std::move(text);
    setTextStatistics(TextAnalysis::analyzeUtf8(text_));
    modified_ = true;
    ++revision_;
}

void Document::markModified() noexcept {
    if (!textBufferLoaded_) {
        return;
    }
    modified_ = true;
    ++revision_;
}

void Document::markClean() noexcept {
    modified_ = false;
}

void Document::clear() noexcept {
    path_.clear();
    text_.clear();
    profile_ = {};
    textStatistics_ = {};
    saveEncoding_ = encoding::Encoding::Utf8;
    writesBom_ = false;
    modified_ = false;
    textBufferLoaded_ = true;
    revision_ = 0;
    diskState_ = {};
    tracksDiskState_ = false;
    requiresExplicitOverwrite_ = false;
}

}
