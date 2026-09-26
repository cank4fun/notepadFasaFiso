#include "notepadFasaFiso/metadata/MetadataStore.hpp"

#include "notepadFasaFiso/persistence/BinaryCodec.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <string>
#include <utility>

namespace nff::metadata {
namespace {

constexpr persistence::Magic magic{
    std::byte{'N'}, std::byte{'F'}, std::byte{'F'}, std::byte{'M'},
    std::byte{'E'}, std::byte{'T'}, std::byte{'0'}, std::byte{'1'}};
constexpr std::size_t maximumPathBytes = 64U * 1024U;
constexpr std::uint64_t maximumSpanCount = 8ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t maximumFontCount = 64ULL * 1024ULL;

[[nodiscard]] std::string pathKey(const std::filesystem::path& path) {
    std::error_code error;
    auto normalized = std::filesystem::weakly_canonical(path, error);
    if (error) {
        error.clear();
        normalized = std::filesystem::absolute(path, error).lexically_normal();
        if (error) {
            normalized = path.lexically_normal();
        }
    }
    const auto encoded = normalized.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

[[nodiscard]] bool validSpan(const TextColorSpan& span) noexcept {
    return span.begin < span.end;
}

[[nodiscard]] bool validSpan(const TextAppearanceSpan& span) noexcept {
    return span.begin < span.end && !span.style.empty();
}

[[nodiscard]] std::uint64_t saturatedAdd(const std::uint64_t left,
                                         const std::uint64_t right) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left + right;
}

void mutateRange(std::vector<TextAppearanceSpan>& spans,
                 const std::uint64_t begin,
                 const std::uint64_t end,
                 const std::function<void(AppearanceStyle&)>& mutation) {
    if (begin >= end) {
        return;
    }

    std::vector<TextAppearanceSpan> result;
    result.reserve(spans.size() + 3U);
    std::uint64_t cursor = begin;

    for (const auto& span : spans) {
        if (span.end <= begin || span.begin >= end) {
            result.push_back(span);
            continue;
        }

        if (span.begin < begin) {
            result.push_back({span.begin, begin, span.style});
        }

        const auto overlapBegin = std::max(span.begin, begin);
        const auto overlapEnd = std::min(span.end, end);
        if (cursor < overlapBegin) {
            AppearanceStyle gapStyle;
            mutation(gapStyle);
            if (!gapStyle.empty()) {
                result.push_back({cursor, overlapBegin, gapStyle});
            }
        }

        auto changed = span.style;
        mutation(changed);
        if (!changed.empty()) {
            result.push_back({overlapBegin, overlapEnd, std::move(changed)});
        }
        cursor = std::max(cursor, overlapEnd);

        if (span.end > end) {
            result.push_back({end, span.end, span.style});
        }
    }

    if (cursor < end) {
        AppearanceStyle gapStyle;
        mutation(gapStyle);
        if (!gapStyle.empty()) {
            result.push_back({cursor, end, std::move(gapStyle)});
        }
    }

    spans = std::move(result);
}

}

const std::vector<TextAppearanceSpan>& TextAppearanceMap::spans() const noexcept {
    return spans_;
}

const std::vector<std::string>& TextAppearanceMap::fontFamilies() const noexcept {
    return fontFamilies_;
}

bool TextAppearanceMap::empty() const noexcept {
    return spans_.empty();
}

bool TextAppearanceMap::hasSpoilers() const noexcept {
    return hasSpoilers_;
}

bool TextAppearanceMap::intersects(const std::uint64_t begin, const std::uint64_t end) const noexcept {
    if (begin >= end || spans_.empty()) {
        return false;
    }
    const auto iterator = std::lower_bound(spans_.begin(), spans_.end(), begin,
                                           [](const auto& span, const auto position) {
                                               return span.end <= position;
                                           });
    return iterator != spans_.end() && iterator->begin < end;
}

AppearanceStyle TextAppearanceMap::styleAt(const std::uint64_t offset) const noexcept {
    const auto iterator = std::lower_bound(spans_.begin(), spans_.end(), offset,
                                           [](const auto& span, const auto position) {
                                               return span.end <= position;
                                           });
    if (iterator != spans_.end() && iterator->begin <= offset && offset < iterator->end) {
        return iterator->style;
    }
    return {};
}

std::optional<FontFamilyId> TextAppearanceMap::internFontFamily(const std::string_view family) {
    if (family.empty() || family.size() > maximumFontFamilyBytes ||
        fontFamilies_.size() >= static_cast<std::size_t>(std::numeric_limits<FontFamilyId>::max())) {
        return std::nullopt;
    }
    const auto found = std::find(fontFamilies_.begin(), fontFamilies_.end(), family);
    if (found != fontFamilies_.end()) {
        return static_cast<FontFamilyId>(std::distance(fontFamilies_.begin(), found) + 1);
    }
    fontFamilies_.emplace_back(family);
    return static_cast<FontFamilyId>(fontFamilies_.size());
}

std::string_view TextAppearanceMap::fontFamily(const FontFamilyId id) const noexcept {
    if (id == 0U || static_cast<std::size_t>(id) > fontFamilies_.size()) {
        return {};
    }
    return fontFamilies_[static_cast<std::size_t>(id - 1U)];
}

void TextAppearanceMap::clear() noexcept {
    spans_.clear();
    fontFamilies_.clear();
    hasSpoilers_ = false;
}

void TextAppearanceMap::setForeground(const std::uint64_t begin,
                                      const std::uint64_t end,
                                      const std::uint32_t argb) {
    mutateRange(spans_, begin, end, [argb](auto& style) { style.foregroundArgb = argb; });
    compactSorted();
}

void TextAppearanceMap::clearForeground(const std::uint64_t begin, const std::uint64_t end) {
    mutateRange(spans_, begin, end, [](auto& style) { style.foregroundArgb.reset(); });
    compactSorted();
}

bool TextAppearanceMap::setFontFamily(const std::uint64_t begin,
                                      const std::uint64_t end,
                                      const FontFamilyId family) {
    if (fontFamily(family).empty()) {
        return false;
    }
    mutateRange(spans_, begin, end, [family](auto& style) { style.fontFamilyId = family; });
    compactSorted();
    return true;
}

void TextAppearanceMap::clearFontFamily(const std::uint64_t begin, const std::uint64_t end) {
    mutateRange(spans_, begin, end, [](auto& style) { style.fontFamilyId.reset(); });
    compactSorted();
}

bool TextAppearanceMap::setFontSize(const std::uint64_t begin,
                                    const std::uint64_t end,
                                    const std::uint8_t points) {
    if (points < minimumFontSizePoints || points > maximumFontSizePoints) {
        return false;
    }
    mutateRange(spans_, begin, end, [points](auto& style) { style.fontSizePoints = points; });
    compactSorted();
    return true;
}

void TextAppearanceMap::clearFontSize(const std::uint64_t begin, const std::uint64_t end) {
    mutateRange(spans_, begin, end, [](auto& style) { style.fontSizePoints.reset(); });
    compactSorted();
}

void TextAppearanceMap::setSpoiler(const std::uint64_t begin,
                                   const std::uint64_t end,
                                   const bool enabled) {
    mutateRange(spans_, begin, end, [enabled](auto& style) { style.spoiler = enabled; });
    compactSorted();
}

void TextAppearanceMap::reset(const std::uint64_t begin, const std::uint64_t end) {
    mutateRange(spans_, begin, end, [](auto& style) { style = {}; });
    compactSorted();
}

void TextAppearanceMap::applyEdit(const std::uint64_t offset,
                                  const std::uint64_t erasedBytes,
                                  const std::uint64_t insertedBytes,
                                  const AppearanceEditPolicy policy) {
    if (spans_.empty() || policy == AppearanceEditPolicy::PreserveState ||
        (policy == AppearanceEditPolicy::PreserveOffsets && erasedBytes == insertedBytes)) {
        return;
    }

    const auto firstAffected = std::lower_bound(spans_.begin(), spans_.end(), offset,
                                                [](const auto& span, const auto position) {
                                                    return span.end <= position;
                                                });

    if (erasedBytes == 0U) {
        if (insertedBytes == 0U) {
            return;
        }
        for (auto iterator = firstAffected; iterator != spans_.end(); ++iterator) {
            if (iterator->begin < offset && offset < iterator->end) {
                iterator->end = saturatedAdd(iterator->end, insertedBytes);
            } else if (iterator->begin >= offset) {
                iterator->begin = saturatedAdd(iterator->begin, insertedBytes);
                iterator->end = saturatedAdd(iterator->end, insertedBytes);
            }
        }
        return;
    }

    const auto erasedEnd = saturatedAdd(offset, erasedBytes);
    std::vector<TextAppearanceSpan> adjusted;
    adjusted.reserve(spans_.size());
    adjusted.insert(adjusted.end(), spans_.begin(), firstAffected);
    for (auto iterator = firstAffected; iterator != spans_.end(); ++iterator) {
        const auto& span = *iterator;
        if (span.begin >= erasedEnd) {
            const auto distance = span.begin - erasedEnd;
            const auto length = span.end - span.begin;
            const auto newBegin = saturatedAdd(offset, saturatedAdd(insertedBytes, distance));
            adjusted.push_back({newBegin, saturatedAdd(newBegin, length), span.style});
            continue;
        }
        if (span.begin < offset) {
            adjusted.push_back({span.begin, offset, span.style});
        }
        if (span.end > erasedEnd) {
            const auto tailLength = span.end - erasedEnd;
            const auto newBegin = saturatedAdd(offset, insertedBytes);
            adjusted.push_back({newBegin, saturatedAdd(newBegin, tailLength), span.style});
        }
    }
    spans_ = std::move(adjusted);
    compactSorted();
}

std::vector<TextAppearanceSpan> TextAppearanceMap::fragment(const std::uint64_t begin,
                                                            const std::uint64_t end) const {
    std::vector<TextAppearanceSpan> result;
    if (begin >= end || spans_.empty()) {
        return result;
    }
    auto iterator = std::lower_bound(spans_.begin(), spans_.end(), begin,
                                     [](const auto& span, const auto position) {
                                         return span.end <= position;
                                     });
    for (; iterator != spans_.end() && iterator->begin < end; ++iterator) {
        result.push_back({std::max(iterator->begin, begin),
                          std::min(iterator->end, end), iterator->style});
    }
    return result;
}

bool TextAppearanceMap::replaceRange(const std::uint64_t begin,
                                     const std::uint64_t end,
                                     std::vector<TextAppearanceSpan> replacement) {
    if (begin >= end || std::any_of(replacement.begin(), replacement.end(), [this, begin, end](const auto& span) {
            return !validSpan(span) || span.begin < begin || span.end > end || !validStyle(span.style);
        })) {
        return false;
    }

    reset(begin, end);
    spans_.insert(spans_.end(), replacement.begin(), replacement.end());
    normalize();
    return true;
}

bool TextAppearanceMap::replace(std::vector<TextAppearanceSpan> spans,
                                std::vector<std::string> fontFamilies) {
    if (fontFamilies.size() > static_cast<std::size_t>(maximumFontCount) ||
        std::any_of(fontFamilies.begin(), fontFamilies.end(), [](const auto& family) {
            return family.empty() || family.size() > maximumFontFamilyBytes;
        })) {
        return false;
    }
    const auto oldFonts = std::move(fontFamilies_);
    fontFamilies_ = std::move(fontFamilies);
    if (std::any_of(spans.begin(), spans.end(), [this](const auto& span) {
            return !validSpan(span) || !validStyle(span.style);
        })) {
        fontFamilies_ = oldFonts;
        return false;
    }
    spans_ = std::move(spans);
    normalize();
    return true;
}

bool TextAppearanceMap::validStyle(const AppearanceStyle& style) const noexcept {
    if (style.empty()) {
        return false;
    }
    if (style.fontFamilyId.has_value() && fontFamily(*style.fontFamilyId).empty()) {
        return false;
    }
    if (style.fontSizePoints.has_value() &&
        (*style.fontSizePoints < minimumFontSizePoints || *style.fontSizePoints > maximumFontSizePoints)) {
        return false;
    }
    return true;
}

void TextAppearanceMap::normalize() {
    spans_.erase(std::remove_if(spans_.begin(), spans_.end(), [](const auto& span) {
                     return !validSpan(span);
                 }),
                 spans_.end());
    std::sort(spans_.begin(), spans_.end(), [](const auto& left, const auto& right) {
        if (left.begin != right.begin) {
            return left.begin < right.begin;
        }
        return left.end < right.end;
    });
    compactSorted();
}

void TextAppearanceMap::compactSorted() {
    std::size_t write = 0U;
    for (const auto& source : spans_) {
        if (!validSpan(source)) {
            continue;
        }
        auto span = source;
        if (write != 0U && spans_[write - 1U].style == span.style &&
            spans_[write - 1U].end >= span.begin) {
            spans_[write - 1U].end = std::max(spans_[write - 1U].end, span.end);
            continue;
        }
        if (write != 0U && spans_[write - 1U].end > span.begin) {
            span.begin = spans_[write - 1U].end;
            if (!validSpan(span)) {
                continue;
            }
        }
        spans_[write++] = std::move(span);
    }
    spans_.resize(write);
    hasSpoilers_ = std::any_of(spans_.begin(), spans_.end(), [](const auto& span) {
        return span.style.spoiler;
    });
}

const std::vector<TextColorSpan>& TextColorMap::spans() const noexcept { return spans_; }
bool TextColorMap::empty() const noexcept { return spans_.empty(); }
bool TextColorMap::intersects(const std::uint64_t begin, const std::uint64_t end) const noexcept {
    if (begin >= end) return false;
    for (const auto& span : spans_) {
        if (span.begin >= end) break;
        if (span.end > begin) return true;
    }
    return false;
}
void TextColorMap::clear() noexcept { spans_.clear(); }
void TextColorMap::setColor(const std::uint64_t begin, const std::uint64_t end, const std::uint32_t argb) {
    if (begin >= end) return;
    clearColor(begin, end);
    spans_.push_back({begin, end, argb});
    normalize();
}
void TextColorMap::clearColor(const std::uint64_t begin, const std::uint64_t end) {
    if (begin >= end || spans_.empty()) return;
    std::vector<TextColorSpan> result;
    result.reserve(spans_.size() + 1U);
    for (const auto& span : spans_) {
        if (span.end <= begin || span.begin >= end) { result.push_back(span); continue; }
        if (span.begin < begin) result.push_back({span.begin, begin, span.argb});
        if (span.end > end) result.push_back({end, span.end, span.argb});
    }
    spans_ = std::move(result);
}
void TextColorMap::applyEdit(const std::uint64_t offset,
                             const std::uint64_t erasedBytes,
                             const std::uint64_t insertedBytes,
                             const TextColorEditPolicy policy) {
    if (policy == TextColorEditPolicy::PreserveState ||
        (policy == TextColorEditPolicy::PreserveOffsets && erasedBytes == insertedBytes)) return;
    const auto erasedEnd = saturatedAdd(offset, erasedBytes);
    std::vector<TextColorSpan> adjusted;
    adjusted.reserve(spans_.size());
    for (const auto& span : spans_) {
        if (span.end <= offset) { adjusted.push_back(span); continue; }
        if (span.begin >= erasedEnd) {
            const auto distance = span.begin - erasedEnd;
            const auto length = span.end - span.begin;
            const auto newBegin = saturatedAdd(offset, saturatedAdd(insertedBytes, distance));
            adjusted.push_back({newBegin, saturatedAdd(newBegin, length), span.argb});
            continue;
        }
        if (span.begin < offset) adjusted.push_back({span.begin, offset, span.argb});
        if (span.end > erasedEnd) {
            const auto tailLength = span.end - erasedEnd;
            const auto newBegin = saturatedAdd(offset, insertedBytes);
            adjusted.push_back({newBegin, saturatedAdd(newBegin, tailLength), span.argb});
        }
    }
    spans_ = std::move(adjusted);
    normalize();
}
bool TextColorMap::replace(std::vector<TextColorSpan> spans) {
    if (std::any_of(spans.begin(), spans.end(), [](const auto& span) { return !validSpan(span); })) return false;
    spans_ = std::move(spans);
    normalize();
    return true;
}
void TextColorMap::normalize() {
    std::sort(spans_.begin(), spans_.end(), [](const auto& left, const auto& right) {
        if (left.begin != right.begin) return left.begin < right.begin;
        return left.end < right.end;
    });
    std::vector<TextColorSpan> result;
    result.reserve(spans_.size());
    for (const auto& span : spans_) {
        if (!validSpan(span)) continue;
        if (!result.empty() && result.back().argb == span.argb && result.back().end >= span.begin) {
            result.back().end = std::max(result.back().end, span.end); continue;
        }
        if (!result.empty() && result.back().end > span.begin) {
            auto clipped = span; clipped.begin = result.back().end;
            if (validSpan(clipped)) result.push_back(clipped);
            continue;
        }
        result.push_back(span);
    }
    spans_ = std::move(result);
}

MetadataStore::MetadataStore(std::filesystem::path root) : root_(std::move(root)) {}
const std::filesystem::path& MetadataStore::root() const noexcept { return root_; }
std::filesystem::path MetadataStore::metadataPath(const std::filesystem::path& documentPath) const {
    const auto hash = persistence::hash64(pathKey(documentPath));
    return root_ / (std::to_string(hash) + ".nff-meta");
}

std::error_code MetadataStore::save(const std::filesystem::path& documentPath,
                                    const std::string_view utf8Text,
                                    const TextAppearanceMap& appearance) {
    if (documentPath.empty()) return std::make_error_code(std::errc::invalid_argument);
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (error) return error;

    persistence::BinaryWriter writer;
    writer.writePath(documentPath);
    writer.writeU64(persistence::hash64(utf8Text));
    writer.writeU64(static_cast<std::uint64_t>(utf8Text.size()));
    writer.writeU64(static_cast<std::uint64_t>(appearance.fontFamilies().size()));
    for (const auto& family : appearance.fontFamilies()) writer.writeString(family);
    writer.writeU64(static_cast<std::uint64_t>(appearance.spans().size()));
    for (const auto& span : appearance.spans()) {
        writer.writeU64(span.begin);
        writer.writeU64(span.end);
        std::uint8_t flags = 0U;
        if (span.style.foregroundArgb) flags |= 0x01U;
        if (span.style.fontFamilyId) flags |= 0x02U;
        if (span.style.fontSizePoints) flags |= 0x04U;
        if (span.style.spoiler) flags |= 0x08U;
        writer.writeU8(flags);
        if (span.style.foregroundArgb) writer.writeU32(*span.style.foregroundArgb);
        if (span.style.fontFamilyId) writer.writeU32(*span.style.fontFamilyId);
        if (span.style.fontSizePoints) writer.writeU8(*span.style.fontSizePoints);
    }
    return persistence::writeEnvelope(metadataPath(documentPath), magic, currentSchemaVersion, writer.bytes());
}

std::error_code MetadataStore::save(const std::filesystem::path& documentPath,
                                    const std::string_view utf8Text,
                                    const TextColorMap& colors) {
    TextAppearanceMap appearance;
    for (const auto& span : colors.spans()) appearance.setForeground(span.begin, span.end, span.argb);
    return save(documentPath, utf8Text, appearance);
}

MetadataLoadResult MetadataStore::load(const std::filesystem::path& documentPath,
                                       const std::string_view currentUtf8Text,
                                       const std::size_t maximumBytes) const {
    if (documentPath.empty()) return {{}, false, std::make_error_code(std::errc::invalid_argument)};
    const auto envelope = persistence::readEnvelope(metadataPath(documentPath), magic, maximumBytes);
    if (!envelope) return {{}, false, envelope.error};
    if (envelope.schemaVersion > currentSchemaVersion) return {{}, false, std::make_error_code(std::errc::protocol_not_supported)};

    persistence::BinaryReader reader(envelope.payload);
    std::filesystem::path storedPath;
    std::uint64_t textHash = 0, textBytes = 0;
    if (!reader.readPath(storedPath, maximumPathBytes) || !reader.readU64(textHash) || !reader.readU64(textBytes)) {
        return {{}, false, std::make_error_code(std::errc::illegal_byte_sequence)};
    }

    TextAppearanceMap appearance;
    if (envelope.schemaVersion == 1U) {
        std::uint64_t spanCount = 0;
        if (!reader.readU64(spanCount) || spanCount > maximumSpanCount) return {{}, false, std::make_error_code(std::errc::illegal_byte_sequence)};
        for (std::uint64_t index = 0; index < spanCount; ++index) {
            std::uint64_t begin = 0, end = 0; std::uint32_t argb = 0;
            if (!reader.readU64(begin) || !reader.readU64(end) || !reader.readU32(argb) || begin >= end) {
                return {{}, false, std::make_error_code(std::errc::illegal_byte_sequence)};
            }
            appearance.setForeground(begin, end, argb);
        }
    } else if (envelope.schemaVersion == 2U) {
        std::uint64_t fontCount = 0;
        if (!reader.readU64(fontCount) || fontCount > maximumFontCount) return {{}, false, std::make_error_code(std::errc::illegal_byte_sequence)};
        std::vector<std::string> fonts;
        fonts.reserve(static_cast<std::size_t>(fontCount));
        for (std::uint64_t index = 0; index < fontCount; ++index) {
            std::string family;
            if (!reader.readString(family, TextAppearanceMap::maximumFontFamilyBytes) || family.empty()) {
                return {{}, false, std::make_error_code(std::errc::illegal_byte_sequence)};
            }
            fonts.push_back(std::move(family));
        }
        std::uint64_t spanCount = 0;
        if (!reader.readU64(spanCount) || spanCount > maximumSpanCount) return {{}, false, std::make_error_code(std::errc::illegal_byte_sequence)};
        std::vector<TextAppearanceSpan> spans;
        spans.reserve(static_cast<std::size_t>(spanCount));
        for (std::uint64_t index = 0; index < spanCount; ++index) {
            TextAppearanceSpan span; std::uint8_t flags = 0U;
            if (!reader.readU64(span.begin) || !reader.readU64(span.end) || !reader.readU8(flags) ||
                span.begin >= span.end || (flags & 0xF0U) != 0U || flags == 0U) {
                return {{}, false, std::make_error_code(std::errc::illegal_byte_sequence)};
            }
            if ((flags & 0x01U) != 0U) { std::uint32_t value=0; if (!reader.readU32(value)) return {{}, false, std::make_error_code(std::errc::illegal_byte_sequence)}; span.style.foregroundArgb=value; }
            if ((flags & 0x02U) != 0U) { std::uint32_t value=0; if (!reader.readU32(value)) return {{}, false, std::make_error_code(std::errc::illegal_byte_sequence)}; span.style.fontFamilyId=value; }
            if ((flags & 0x04U) != 0U) { std::uint8_t value=0; if (!reader.readU8(value)) return {{}, false, std::make_error_code(std::errc::illegal_byte_sequence)}; span.style.fontSizePoints=value; }
            span.style.spoiler = (flags & 0x08U) != 0U;
            spans.push_back(std::move(span));
        }
        if (!appearance.replace(std::move(spans), std::move(fonts))) {
            return {{}, false, std::make_error_code(std::errc::illegal_byte_sequence)};
        }
    } else {
        return {{}, false, std::make_error_code(std::errc::protocol_not_supported)};
    }

    if (!reader.empty() || pathKey(storedPath) != pathKey(documentPath)) {
        return {{}, false, std::make_error_code(std::errc::illegal_byte_sequence)};
    }

    DocumentMetadata metadata;
    metadata.textHash = textHash;
    metadata.textBytes = textBytes;
    metadata.appearance = std::move(appearance);
    for (const auto& span : metadata.appearance.spans()) {
        if (span.style.foregroundArgb) metadata.colors.setColor(span.begin, span.end, *span.style.foregroundArgb);
    }
    const bool stale = textBytes != static_cast<std::uint64_t>(currentUtf8Text.size()) || textHash != persistence::hash64(currentUtf8Text);
    return {std::move(metadata), stale, {}};
}

std::error_code MetadataStore::erase(const std::filesystem::path& documentPath) noexcept {
    if (documentPath.empty()) return std::make_error_code(std::errc::invalid_argument);
    std::error_code error;
    std::filesystem::remove(metadataPath(documentPath), error);
    if (error == std::errc::no_such_file_or_directory) error.clear();
    return error;
}

}
