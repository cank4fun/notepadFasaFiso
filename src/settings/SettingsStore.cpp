#include "notepadFasaFiso/settings/SettingsStore.hpp"

#include "notepadFasaFiso/persistence/BinaryCodec.hpp"

#include <array>
#include <chrono>
#include <limits>

namespace nff::settings {
namespace {

constexpr persistence::Magic magic{
    std::byte{'N'}, std::byte{'F'}, std::byte{'F'}, std::byte{'S'},
    std::byte{'E'}, std::byte{'T'}, std::byte{'0'}, std::byte{'1'}};
constexpr std::size_t maximumFontNameBytes = 16U * 1024U;
constexpr std::size_t maximumSidebarRootBytes = 64U * 1024U;

template <typename Enum>
[[nodiscard]] bool enumWithin(const std::uint8_t value, const Enum maximum) noexcept {
    return value <= static_cast<std::uint8_t>(maximum);
}

[[nodiscard]] bool millisecondsToU64(const std::chrono::milliseconds value,
                                     std::uint64_t& output) noexcept {
    if (value.count() < 0) {
        return false;
    }
    output = static_cast<std::uint64_t>(value.count());
    return true;
}

[[nodiscard]] bool readCommon(persistence::BinaryReader& reader,
                              AppSettings& settings,
                              std::uint8_t& theme,
                              std::uint8_t& viewerPerformance,
                              std::uint64_t& viewerThreshold,
                              std::uint64_t& longLineThreshold,
                              std::uint64_t& sampleBytes,
                              std::uint64_t& recoveryDelay,
                              std::uint64_t& saveDelay) {
    return reader.readU8(theme) && reader.readString(settings.fontFamily, maximumFontNameBytes) &&
           reader.readDouble(settings.fontPointSize) && reader.readBool(settings.wordWrap) &&
           reader.readBool(settings.showLineNumbers) && reader.readBool(settings.highlightUrls) &&
           reader.readBool(settings.tabsEnabled) && reader.readBool(settings.sidebarVisible) &&
           reader.readBool(settings.restorePreviousSession) && reader.readU8(viewerPerformance) &&
           reader.readU64(viewerThreshold) && reader.readU64(longLineThreshold) &&
           reader.readU64(sampleBytes) && reader.readBool(settings.autoSave.recoveryEnabled) &&
           reader.readU64(recoveryDelay) && reader.readBool(settings.autoSave.saveRealFiles) &&
           reader.readU64(saveDelay) && reader.readBool(settings.autoSave.saveOnFocusLoss);
}

[[nodiscard]] bool finalizeLoadedSettings(AppSettings& settings,
                                          const std::uint8_t theme,
                                          const std::uint8_t viewerPerformance,
                                          const std::uint64_t viewerThreshold,
                                          const std::uint64_t longLineThreshold,
                                          const std::uint64_t sampleBytes,
                                          const std::uint64_t recoveryDelay,
                                          const std::uint64_t saveDelay) {
    if (!enumWithin(theme, ThemePreference::Dark) ||
        !enumWithin(viewerPerformance, viewer::PerformanceProfile::MemorySaver) ||
        viewerThreshold > static_cast<std::uint64_t>(std::numeric_limits<std::uintmax_t>::max()) ||
        longLineThreshold > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        sampleBytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        recoveryDelay > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        saveDelay > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return false;
    }

    settings.theme = static_cast<ThemePreference>(theme);
    settings.viewerPerformance = static_cast<viewer::PerformanceProfile>(viewerPerformance);
    settings.inspectOptions.viewerSizeThreshold = static_cast<std::uintmax_t>(viewerThreshold);
    settings.inspectOptions.longLineThreshold = static_cast<std::size_t>(longLineThreshold);
    settings.inspectOptions.sampleBytes = static_cast<std::size_t>(sampleBytes);
    settings.autoSave.recoveryDelay =
        std::chrono::milliseconds(static_cast<std::int64_t>(recoveryDelay));
    settings.autoSave.saveDelay = std::chrono::milliseconds(static_cast<std::int64_t>(saveDelay));
    return validate(settings);
}

[[nodiscard]] SettingsLoadResult loadV1(const std::span<const std::byte> payload) {
    persistence::BinaryReader reader(payload);
    AppSettings settings;
    std::uint8_t theme = 0;
    std::uint8_t viewerPerformance = 0;
    std::uint64_t viewerThreshold = 0;
    std::uint64_t longLineThreshold = 0;
    std::uint64_t sampleBytes = 0;
    std::uint64_t recoveryDelay = 0;
    std::uint64_t saveDelay = 0;

    if (!readCommon(reader, settings, theme, viewerPerformance, viewerThreshold,
                    longLineThreshold, sampleBytes, recoveryDelay, saveDelay) || !reader.empty() ||
        !finalizeLoadedSettings(settings, theme, viewerPerformance, viewerThreshold,
                                longLineThreshold, sampleBytes, recoveryDelay, saveDelay)) {
        return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }
    settings.highlightUrls = false;
    return {std::move(settings), {}};
}

[[nodiscard]] SettingsLoadResult loadV2(const std::span<const std::byte> payload) {
    persistence::BinaryReader reader(payload);
    AppSettings settings;
    std::uint8_t theme = 0;
    std::uint8_t accent = 0;
    std::uint8_t density = 0;
    std::uint8_t viewerPerformance = 0;
    std::uint64_t viewerThreshold = 0;
    std::uint64_t longLineThreshold = 0;
    std::uint64_t sampleBytes = 0;
    std::uint64_t recoveryDelay = 0;
    std::uint64_t saveDelay = 0;

    if (!reader.readU8(theme) || !reader.readU8(accent) || !reader.readU8(density) ||
        !reader.readString(settings.fontFamily, maximumFontNameBytes) ||
        !reader.readDouble(settings.fontPointSize) || !reader.readBool(settings.wordWrap) ||
        !reader.readBool(settings.showLineNumbers) || !reader.readBool(settings.highlightUrls) ||
        !reader.readBool(settings.tabsEnabled) || !reader.readBool(settings.sidebarVisible) ||
        !reader.readBool(settings.restorePreviousSession) || !reader.readU8(viewerPerformance) ||
        !reader.readU64(viewerThreshold) || !reader.readU64(longLineThreshold) ||
        !reader.readU64(sampleBytes) || !reader.readBool(settings.autoSave.recoveryEnabled) ||
        !reader.readU64(recoveryDelay) || !reader.readBool(settings.autoSave.saveRealFiles) ||
        !reader.readU64(saveDelay) || !reader.readBool(settings.autoSave.saveOnFocusLoss) ||
        !reader.empty() || !enumWithin(accent, AccentPreference::Amber) ||
        !enumWithin(density, UiDensity::Comfortable)) {
        return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }

    settings.accent = static_cast<AccentPreference>(accent);
    settings.density = static_cast<UiDensity>(density);
    if (!finalizeLoadedSettings(settings, theme, viewerPerformance, viewerThreshold,
                                longLineThreshold, sampleBytes, recoveryDelay, saveDelay)) {
        return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }
    settings.highlightUrls = false;
    return {std::move(settings), {}};
}

[[nodiscard]] SettingsLoadResult loadV3(const std::span<const std::byte> payload) {
    persistence::BinaryReader reader(payload);
    AppSettings settings;
    std::uint8_t theme = 0;
    std::uint8_t accent = 0;
    std::uint8_t density = 0;
    std::uint8_t viewerPerformance = 0;
    std::uint64_t viewerThreshold = 0;
    std::uint64_t longLineThreshold = 0;
    std::uint64_t sampleBytes = 0;
    std::uint64_t recoveryDelay = 0;
    std::uint64_t saveDelay = 0;

    if (!reader.readU8(theme) || !reader.readU8(accent) || !reader.readU8(density) ||
        !reader.readString(settings.fontFamily, maximumFontNameBytes) ||
        !reader.readDouble(settings.fontPointSize) || !reader.readBool(settings.wordWrap) ||
        !reader.readBool(settings.showLineNumbers) || !reader.readBool(settings.highlightUrls) ||
        !reader.readBool(settings.tabsEnabled) || !reader.readBool(settings.sidebarVisible) ||
        !reader.readBool(settings.restorePreviousSession) || !reader.readU8(viewerPerformance) ||
        !reader.readU64(viewerThreshold) || !reader.readU64(longLineThreshold) ||
        !reader.readU64(sampleBytes) || !reader.readBool(settings.autoSave.recoveryEnabled) ||
        !reader.readU64(recoveryDelay) || !reader.readBool(settings.autoSave.saveRealFiles) ||
        !reader.readU64(saveDelay) || !reader.readBool(settings.autoSave.saveOnFocusLoss) ||
        !reader.readString(settings.sidebarRootUtf8, maximumSidebarRootBytes) || !reader.empty() ||
        !enumWithin(accent, AccentPreference::Amber) ||
        !enumWithin(density, UiDensity::Comfortable)) {
        return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }

    settings.accent = static_cast<AccentPreference>(accent);
    settings.density = static_cast<UiDensity>(density);
    if (!finalizeLoadedSettings(settings, theme, viewerPerformance, viewerThreshold,
                                longLineThreshold, sampleBytes, recoveryDelay, saveDelay)) {
        return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }
    settings.highlightUrls = false;
    return {std::move(settings), {}};
}

[[nodiscard]] SettingsLoadResult loadV4OrV5(const std::span<const std::byte> payload,
                                                      const bool preserveLinkDetection) {
    persistence::BinaryReader reader(payload);
    AppSettings settings;
    std::uint8_t theme = 0;
    std::uint8_t accent = 0;
    std::uint8_t density = 0;
    std::uint8_t viewerPerformance = 0;
    std::uint64_t viewerThreshold = 0;
    std::uint64_t longLineThreshold = 0;
    std::uint64_t sampleBytes = 0;
    std::uint64_t recoveryDelay = 0;
    std::uint64_t saveDelay = 0;

    bool retiredSidebarLiveUpdates = false;

    if (!reader.readU8(theme) || !reader.readU8(accent) || !reader.readU8(density) ||
        !reader.readString(settings.fontFamily, maximumFontNameBytes) ||
        !reader.readDouble(settings.fontPointSize) || !reader.readBool(settings.wordWrap) ||
        !reader.readBool(settings.showLineNumbers) || !reader.readBool(settings.highlightUrls) ||
        !reader.readBool(settings.tabsEnabled) || !reader.readBool(settings.sidebarVisible) ||
        !reader.readBool(settings.restorePreviousSession) || !reader.readU8(viewerPerformance) ||
        !reader.readU64(viewerThreshold) || !reader.readU64(longLineThreshold) ||
        !reader.readU64(sampleBytes) || !reader.readBool(settings.autoSave.recoveryEnabled) ||
        !reader.readU64(recoveryDelay) || !reader.readBool(settings.autoSave.saveRealFiles) ||
        !reader.readU64(saveDelay) || !reader.readBool(settings.autoSave.saveOnFocusLoss) ||
        !reader.readString(settings.sidebarRootUtf8, maximumSidebarRootBytes) ||
        !reader.readBool(retiredSidebarLiveUpdates) || !reader.empty() ||
        !enumWithin(accent, AccentPreference::Amber) ||
        !enumWithin(density, UiDensity::Comfortable)) {
        return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }

    settings.accent = static_cast<AccentPreference>(accent);
    settings.density = static_cast<UiDensity>(density);
    if (!finalizeLoadedSettings(settings, theme, viewerPerformance, viewerThreshold,
                                longLineThreshold, sampleBytes, recoveryDelay, saveDelay)) {
        return {{}, std::make_error_code(std::errc::illegal_byte_sequence)};
    }
    if (!preserveLinkDetection) {

        settings.highlightUrls = false;
    }
    return {std::move(settings), {}};
}

}

std::error_code SettingsStore::save(const std::filesystem::path& path,
                                    const AppSettings& settings) {
    if (!validate(settings)) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    std::uint64_t recoveryDelay = 0;
    std::uint64_t saveDelay = 0;
    if (!millisecondsToU64(settings.autoSave.recoveryDelay, recoveryDelay) ||
        !millisecondsToU64(settings.autoSave.saveDelay, saveDelay)) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    persistence::BinaryWriter writer;
    writer.writeU8(static_cast<std::uint8_t>(settings.theme));
    writer.writeU8(static_cast<std::uint8_t>(settings.accent));
    writer.writeU8(static_cast<std::uint8_t>(settings.density));
    writer.writeString(settings.fontFamily);
    writer.writeDouble(settings.fontPointSize);
    writer.writeBool(settings.wordWrap);
    writer.writeBool(settings.showLineNumbers);
    writer.writeBool(settings.highlightUrls);
    writer.writeBool(settings.tabsEnabled);
    writer.writeBool(settings.sidebarVisible);
    writer.writeBool(settings.restorePreviousSession);
    writer.writeU8(static_cast<std::uint8_t>(settings.viewerPerformance));
    writer.writeU64(static_cast<std::uint64_t>(settings.inspectOptions.viewerSizeThreshold));
    writer.writeU64(static_cast<std::uint64_t>(settings.inspectOptions.longLineThreshold));
    writer.writeU64(static_cast<std::uint64_t>(settings.inspectOptions.sampleBytes));
    writer.writeBool(settings.autoSave.recoveryEnabled);
    writer.writeU64(recoveryDelay);
    writer.writeBool(settings.autoSave.saveRealFiles);
    writer.writeU64(saveDelay);
    writer.writeBool(settings.autoSave.saveOnFocusLoss);
    writer.writeString(settings.sidebarRootUtf8);
    writer.writeBool(false);

    return persistence::writeEnvelope(path, magic, currentSchemaVersion, writer.bytes());
}

SettingsLoadResult SettingsStore::load(const std::filesystem::path& path) {
    const auto envelope = persistence::readEnvelope(path, magic, maximumSettingsBytes);
    if (!envelope) {
        return {{}, envelope.error};
    }
    if (envelope.schemaVersion > currentSchemaVersion) {
        return {{}, std::make_error_code(std::errc::protocol_not_supported)};
    }

    switch (envelope.schemaVersion) {
    case 1U:
        return loadV1(envelope.payload);
    case 2U:
        return loadV2(envelope.payload);
    case 3U:
        return loadV3(envelope.payload);
    case 4U:
        return loadV4OrV5(envelope.payload, false);
    case 5U:
        return loadV4OrV5(envelope.payload, true);
    default:
        return {{}, std::make_error_code(std::errc::protocol_not_supported)};
    }
}

}
