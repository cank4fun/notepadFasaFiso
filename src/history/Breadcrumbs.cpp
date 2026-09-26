#include "notepadFasaFiso/history/Breadcrumbs.hpp"

#include "notepadFasaFiso/persistence/BinaryCodec.hpp"

#include <array>
#include <limits>
#include <vector>

namespace nff::history {
namespace {

constexpr persistence::Magic magic{
    std::byte{'N'}, std::byte{'F'}, std::byte{'F'}, std::byte{'R'},
    std::byte{'E'}, std::byte{'C'}, std::byte{'0'}, std::byte{'1'}};
constexpr std::size_t maximumPathBytes = 64U * 1024U;

[[nodiscard]] RecentFilesLoadResult loadV1(const std::span<const std::byte> payload,
                                           const std::size_t capacity) {
    persistence::BinaryReader reader(payload);
    std::uint64_t count = 0;
    if (!reader.readU64(count) || count > RecentFiles::maximumCapacity) {
        return {RecentFiles(capacity), std::make_error_code(std::errc::illegal_byte_sequence)};
    }

    std::vector<RecentFileEntry> entries;
    entries.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
        RecentFileEntry entry;
        std::uint8_t mode = 0;
        if (!reader.readPath(entry.path, maximumPathBytes) ||
            !reader.readU64(entry.lastOpenedUnixMilliseconds) ||
            !reader.readU64(entry.openCount) || !reader.readU8(mode) ||
            mode > static_cast<std::uint8_t>(core::OpenMode::BinaryPreview)) {
            return {RecentFiles(capacity), std::make_error_code(std::errc::illegal_byte_sequence)};
        }
        entry.lastOpenMode = static_cast<core::OpenMode>(mode);
        entries.push_back(std::move(entry));
    }
    if (!reader.empty()) {
        return {RecentFiles(capacity), std::make_error_code(std::errc::illegal_byte_sequence)};
    }

    RecentFiles recent(capacity);
    if (!recent.replace(std::move(entries))) {
        return {RecentFiles(capacity), std::make_error_code(std::errc::illegal_byte_sequence)};
    }
    return {std::move(recent), {}};
}

}

std::error_code Breadcrumbs::save(const std::filesystem::path& path,
                                       const RecentFiles& recent) {
    persistence::BinaryWriter writer;
    writer.writeU64(static_cast<std::uint64_t>(recent.size()));
    for (const auto& entry : recent.entries()) {
        writer.writePath(entry.path);
        writer.writeU64(entry.lastOpenedUnixMilliseconds);
        writer.writeU64(entry.openCount);
        writer.writeU8(static_cast<std::uint8_t>(entry.lastOpenMode));
    }
    return persistence::writeEnvelope(path, magic, currentSchemaVersion, writer.bytes());
}

RecentFilesLoadResult Breadcrumbs::load(const std::filesystem::path& path,
                                             const std::size_t capacity) {
    const auto envelope = persistence::readEnvelope(path, magic, maximumStoreBytes);
    if (!envelope) {
        return {RecentFiles(capacity), envelope.error};
    }
    if (envelope.schemaVersion > currentSchemaVersion) {
        return {RecentFiles(capacity), std::make_error_code(std::errc::protocol_not_supported)};
    }

    switch (envelope.schemaVersion) {
    case 1U:
        return loadV1(envelope.payload, capacity);
    default:
        return {RecentFiles(capacity), std::make_error_code(std::errc::protocol_not_supported)};
    }
}

}
