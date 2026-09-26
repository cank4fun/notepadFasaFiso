#include "notepadFasaFiso/recovery/RecoveryManager.hpp"

#include "notepadFasaFiso/encoding/TextCodec.hpp"
#include "notepadFasaFiso/platform/Platform.hpp"
#include "notepadFasaFiso/storage/FileReader.hpp"
#include "notepadFasaFiso/storage/FileWriter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <random>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace nff::recovery {
namespace {

constexpr std::array<std::byte, 8> magic{
    std::byte{'N'}, std::byte{'F'}, std::byte{'F'}, std::byte{'R'},
    std::byte{'E'}, std::byte{'C'}, std::byte{'0'}, std::byte{'1'}};
constexpr std::uint32_t schemaVersion = 2;
constexpr std::uint32_t legacySchemaVersion = 1;
constexpr std::uint64_t maximumSpanCount = 8ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t maximumFontCount = 64ULL * 1024ULL;

[[nodiscard]] std::uint64_t unixMillisecondsNow() noexcept {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return milliseconds > 0 ? static_cast<std::uint64_t>(milliseconds) : 0;
}

template <typename Integer>
void appendLittleEndian(std::vector<std::byte>& output, const Integer value) {
    using Unsigned = std::make_unsigned_t<Integer>;
    auto current = static_cast<std::uint64_t>(static_cast<Unsigned>(value));
    for (std::size_t index = 0; index < sizeof(Unsigned); ++index) {
        output.push_back(static_cast<std::byte>(current & 0xFFU));
        current >>= 8U;
    }
}

template <typename Integer>
[[nodiscard]] bool readLittleEndian(const std::span<const std::byte> input,
                                    std::size_t& offset,
                                    Integer& output) noexcept {
    using Unsigned = std::make_unsigned_t<Integer>;
    if (offset > input.size() || input.size() - offset < sizeof(Unsigned)) {
        return false;
    }

    static_assert(sizeof(Unsigned) <= sizeof(std::uint64_t));
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < sizeof(Unsigned); ++index) {
        const auto byte = std::to_integer<std::uint8_t>(input[offset + index]);
        value |= static_cast<std::uint64_t>(byte) << static_cast<unsigned int>(index * 8U);
    }
    offset += sizeof(Unsigned);
    output = static_cast<Integer>(static_cast<Unsigned>(value));
    return true;
}

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::filesystem::path pathFromUtf8(const std::string_view value) {
    std::u8string converted;
    converted.reserve(value.size());
    for (const auto character : value) {
        converted.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    }
    return std::filesystem::path(converted);
}

[[nodiscard]] bool validEncoding(const std::uint8_t value) noexcept {
    return value <= static_cast<std::uint8_t>(encoding::Encoding::Binary);
}

[[nodiscard]] bool validLineEnding(const std::uint8_t value) noexcept {
    return value <= static_cast<std::uint8_t>(core::LineEnding::Mixed);
}

[[nodiscard]] bool appearanceFitsText(
    const metadata::TextAppearanceMap& appearance,
    const std::size_t textBytes) noexcept {
    return std::all_of(
        appearance.spans().begin(), appearance.spans().end(),
        [textBytes](const auto& span) {
            return span.begin < span.end &&
                   span.end <= static_cast<std::uint64_t>(textBytes);
        });
}

void appendString(std::vector<std::byte>& output, const std::string_view value) {
    appendLittleEndian(output, static_cast<std::uint64_t>(value.size()));
    const auto* bytes = reinterpret_cast<const std::byte*>(value.data());
    output.insert(output.end(), bytes, bytes + value.size());
}

[[nodiscard]] bool readString(const std::span<const std::byte> input,
                              std::size_t& offset,
                              std::string& value,
                              const std::size_t maximumBytes) {
    std::uint64_t encodedSize = 0U;
    if (!readLittleEndian(input, offset, encodedSize) ||
        encodedSize > static_cast<std::uint64_t>(maximumBytes) ||
        encodedSize > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    const auto size = static_cast<std::size_t>(encodedSize);
    if (offset > input.size() || size > input.size() - offset) {
        return false;
    }
    const auto* begin = reinterpret_cast<const char*>(input.data() + offset);
    value.assign(begin, size);
    offset += size;
    return true;
}

[[nodiscard]] std::vector<std::byte> serialize(
    const core::DocumentId documentId,
    const core::Document& document,
    const metadata::TextAppearanceMap* appearance) {
    const auto path = pathToUtf8(document.path());
    const auto text = document.text();

    std::vector<std::byte> output;
    output.reserve(magic.size() + 64U + path.size() + text.size());
    output.insert(output.end(), magic.begin(), magic.end());
    appendLittleEndian(output, schemaVersion);
    appendLittleEndian(output, documentId.value);
    appendLittleEndian(output, unixMillisecondsNow());
    appendLittleEndian(output, static_cast<std::uint8_t>(document.saveEncoding()));
    appendLittleEndian(output, static_cast<std::uint8_t>(document.profile().lineEnding));
    appendLittleEndian(output, static_cast<std::uint8_t>(document.writesBom() ? 1U : 0U));
    appendLittleEndian(output, static_cast<std::uint8_t>(0U));
    appendLittleEndian(output, static_cast<std::uint64_t>(path.size()));
    appendLittleEndian(output, static_cast<std::uint64_t>(text.size()));

    const auto* pathBytes = reinterpret_cast<const std::byte*>(path.data());
    output.insert(output.end(), pathBytes, pathBytes + path.size());
    const auto* textBytes = reinterpret_cast<const std::byte*>(text.data());
    output.insert(output.end(), textBytes, textBytes + text.size());

    const metadata::TextAppearanceMap emptyAppearance;
    const auto& effectiveAppearance = appearance != nullptr ? *appearance : emptyAppearance;
    appendLittleEndian(
        output, static_cast<std::uint64_t>(effectiveAppearance.fontFamilies().size()));
    for (const auto& family : effectiveAppearance.fontFamilies()) {
        appendString(output, family);
    }

    appendLittleEndian(output, static_cast<std::uint64_t>(effectiveAppearance.spans().size()));
    for (const auto& span : effectiveAppearance.spans()) {
        appendLittleEndian(output, span.begin);
        appendLittleEndian(output, span.end);
        std::uint8_t flags = 0U;
        if (span.style.foregroundArgb.has_value()) flags |= 0x01U;
        if (span.style.fontFamilyId.has_value()) flags |= 0x02U;
        if (span.style.fontSizePoints.has_value()) flags |= 0x04U;
        if (span.style.spoiler) flags |= 0x08U;
        appendLittleEndian(output, flags);
        if (span.style.foregroundArgb.has_value()) {
            appendLittleEndian(output, *span.style.foregroundArgb);
        }
        if (span.style.fontFamilyId.has_value()) {
            appendLittleEndian(output, *span.style.fontFamilyId);
        }
        if (span.style.fontSizePoints.has_value()) {
            appendLittleEndian(output, *span.style.fontSizePoints);
        }
    }
    return output;
}

}

RecoveryManager::RecoveryManager(std::filesystem::path root, const std::uint64_t sessionId)
    : root_(std::move(root)), sessionId_(sessionId == 0 ? generateSessionId() : sessionId) {}

std::uint64_t RecoveryManager::sessionId() const noexcept {
    return sessionId_;
}

const std::filesystem::path& RecoveryManager::root() const noexcept {
    return root_;
}

void RecoveryManager::setAppearanceProvider(AppearanceProvider provider) {
    appearanceProvider_ = std::move(provider);
}

std::filesystem::path RecoveryManager::snapshotPath(const core::DocumentId documentId) const {
    const auto name = std::to_string(sessionId_) + "-" + std::to_string(documentId.value) +
                      ".nff-recovery";
    return root_ / name;
}

std::error_code RecoveryManager::checkpoint(
    const core::DocumentId documentId,
    const core::Document& document,
    const metadata::TextAppearanceMap* appearance) {
    if (!documentId) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    if (!document.modified()) {
        return discard(documentId);
    }

    if (const auto directoryError = platform::ensurePrivateDirectory(root_)) {
        return directoryError;
    }

    if (appearance == nullptr && appearanceProvider_) {
        appearance = appearanceProvider_(documentId);
    }
    if (appearance != nullptr && !appearanceFitsText(*appearance, document.text().size())) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    const auto payload = serialize(documentId, document, appearance);
    if (payload.size() > defaultMaximumSnapshotBytes) {
        return std::make_error_code(std::errc::file_too_large);
    }

    const auto path = snapshotPath(documentId);
    if (const auto writeError = storage::FileWriter::writeAtomically(path, payload)) {
        return writeError;
    }
    return platform::hardenPrivateFile(path);
}

std::error_code RecoveryManager::discard(const core::DocumentId documentId) noexcept {
    if (!documentId) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    return discardSnapshot(snapshotPath(documentId));
}

std::error_code RecoveryManager::discardSnapshot(const std::filesystem::path& path) noexcept {
    if (path.empty() || path.extension() != ".nff-recovery" ||
        path.parent_path().lexically_normal() != root_.lexically_normal()) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    std::error_code error;
    const auto removed = std::filesystem::remove(path, error);
    static_cast<void>(removed);
    if (error == std::errc::no_such_file_or_directory) {
        error.clear();
    }
    return error;
}

RecoveryLoadResult RecoveryManager::load(const std::filesystem::path& path,
                                         const std::size_t maximumBytes) const {
    if (path.empty() || path.extension() != ".nff-recovery" ||
        path.parent_path().lexically_normal() != root_.lexically_normal()) {
        return {{}, std::make_error_code(std::errc::invalid_argument)};
    }

    const auto read = storage::FileReader::readAll(path, maximumBytes);
    if (!read) {
        return {{}, read.error};
    }

    const auto input = std::span<const std::byte>(read.bytes);
    if (input.size() < magic.size() ||
        !std::equal(magic.begin(), magic.end(), input.begin())) {
        return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }

    std::size_t offset = magic.size();
    std::uint32_t version = 0;
    std::uint64_t documentId = 0;
    std::uint64_t captured = 0;
    std::uint8_t encodingValue = 0;
    std::uint8_t lineEndingValue = 0;
    std::uint8_t bomValue = 0;
    std::uint8_t reserved = 0;
    std::uint64_t pathSize = 0;
    std::uint64_t textSize = 0;

    if (!readLittleEndian(input, offset, version) ||
        !readLittleEndian(input, offset, documentId) ||
        !readLittleEndian(input, offset, captured) ||
        !readLittleEndian(input, offset, encodingValue) ||
        !readLittleEndian(input, offset, lineEndingValue) ||
        !readLittleEndian(input, offset, bomValue) ||
        !readLittleEndian(input, offset, reserved) ||
        !readLittleEndian(input, offset, pathSize) ||
        !readLittleEndian(input, offset, textSize)) {
        return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }

    if ((version != legacySchemaVersion && version != schemaVersion) || documentId == 0 ||
        !validEncoding(encodingValue) || !validLineEnding(lineEndingValue) ||
        bomValue > 1U || reserved != 0U) {
        return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }
    if (pathSize > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        textSize > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return {{}, std::make_error_code(std::errc::value_too_large)};
    }

    const auto pathLength = static_cast<std::size_t>(pathSize);
    const auto textLength = static_cast<std::size_t>(textSize);
    if (offset > input.size() || pathLength > input.size() - offset) {
        return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }
    const auto pathBytes = input.subspan(offset, pathLength);
    offset += pathLength;
    if (offset > input.size() || textLength > input.size() - offset) {
        return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }
    const auto textBytes = input.subspan(offset, textLength);
    offset += textLength;

    metadata::TextAppearanceMap appearance;
    if (version == legacySchemaVersion) {
        if (offset != input.size()) {
            return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
        }
    } else {
        std::uint64_t fontCount = 0U;
        if (!readLittleEndian(input, offset, fontCount) || fontCount > maximumFontCount) {
            return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
        }
        std::vector<std::string> fonts;
        fonts.reserve(static_cast<std::size_t>(fontCount));
        for (std::uint64_t index = 0U; index < fontCount; ++index) {
            std::string family;
            if (!readString(input, offset, family,
                            metadata::TextAppearanceMap::maximumFontFamilyBytes) ||
                family.empty()) {
                return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
            }
            fonts.push_back(std::move(family));
        }

        std::uint64_t spanCount = 0U;
        constexpr std::size_t minimumSpanBytes =
            sizeof(std::uint64_t) * 2U + sizeof(std::uint8_t);
        if (!readLittleEndian(input, offset, spanCount) || spanCount > maximumSpanCount ||
            offset > input.size() ||
            spanCount > static_cast<std::uint64_t>((input.size() - offset) / minimumSpanBytes)) {
            return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
        }

        std::vector<metadata::TextAppearanceSpan> spans;
        spans.reserve(static_cast<std::size_t>(spanCount));
        for (std::uint64_t index = 0U; index < spanCount; ++index) {
            metadata::TextAppearanceSpan span;
            std::uint8_t flags = 0U;
            if (!readLittleEndian(input, offset, span.begin) ||
                !readLittleEndian(input, offset, span.end) ||
                !readLittleEndian(input, offset, flags) || span.begin >= span.end ||
                span.end > static_cast<std::uint64_t>(textLength) ||
                flags == 0U || (flags & 0xF0U) != 0U) {
                return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
            }
            if ((flags & 0x01U) != 0U) {
                std::uint32_t argb = 0U;
                if (!readLittleEndian(input, offset, argb)) {
                    return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
                }
                span.style.foregroundArgb = argb;
            }
            if ((flags & 0x02U) != 0U) {
                metadata::FontFamilyId family = 0U;
                if (!readLittleEndian(input, offset, family)) {
                    return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
                }
                span.style.fontFamilyId = family;
            }
            if ((flags & 0x04U) != 0U) {
                std::uint8_t points = 0U;
                if (!readLittleEndian(input, offset, points)) {
                    return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
                }
                span.style.fontSizePoints = points;
            }
            span.style.spoiler = (flags & 0x08U) != 0U;
            spans.push_back(std::move(span));
        }

        if (offset != input.size() ||
            !appearance.replace(std::move(spans), std::move(fonts))) {
            return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
        }
    }

    const auto decodedPath = encoding::TextCodec::decode(pathBytes, encoding::Encoding::Utf8);
    const auto decodedText = encoding::TextCodec::decode(textBytes, encoding::Encoding::Utf8);
    if (!decodedPath || !decodedText) {
        return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }

    RecoverySnapshot snapshot;
    snapshot.documentId = core::DocumentId{documentId};
    snapshot.originalPath = pathFromUtf8(decodedPath.text);
    snapshot.text = decodedText.text;
    snapshot.encoding = static_cast<encoding::Encoding>(encodingValue);
    snapshot.lineEnding = static_cast<core::LineEnding>(lineEndingValue);
    snapshot.writeBom = bomValue != 0U;
    snapshot.capturedUnixMilliseconds = captured;
    snapshot.appearance = std::move(appearance);
    return {std::move(snapshot), {}};
}

std::vector<std::filesystem::path> RecoveryManager::list(std::error_code& error) const {
    error.clear();
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::exists(root_, error)) {
        if (error) {
            return {};
        }
        return result;
    }

    for (std::filesystem::directory_iterator iterator(root_, error), end; !error && iterator != end;
         iterator.increment(error)) {
        if (!iterator->is_regular_file(error)) {
            if (error) {
                break;
            }
            continue;
        }
        if (iterator->path().extension() == ".nff-recovery") {
            result.push_back(iterator->path());
        }
    }
    if (error) {
        return {};
    }

    std::ranges::sort(result);
    return result;
}

std::uint64_t RecoveryManager::generateSessionId() noexcept {
    const auto time = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        std::random_device random;
        const auto high = static_cast<std::uint64_t>(random()) << 32U;
        const auto low = static_cast<std::uint64_t>(random());
        const auto generated = (high ^ low ^ time) | 1ULL;
        return generated == 0 ? 1ULL : generated;
    } catch (...) {
        return time == 0 ? 1ULL : (time | 1ULL);
    }
}

}
