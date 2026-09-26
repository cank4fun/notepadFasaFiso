#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/metadata/MetadataStore.hpp"
#include "notepadFasaFiso/persistence/BinaryCodec.hpp"
#include "notepadFasaFiso/session/SessionStore.hpp"
#include "notepadFasaFiso/settings/SettingsStore.hpp"
#include "notepadFasaFiso/storage/FileReader.hpp"
#include "notepadFasaFiso/storage/FileWriter.hpp"
#include "notepadFasaFiso/workspace/WorkspaceModel.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <random>
#include <span>
#include <string_view>
#include <vector>

#ifdef __linux__
#include <sys/stat.h>
#endif

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] std::filesystem::path freshTempDirectory(const std::string_view name) {
    const auto root = std::filesystem::temp_directory_path() / std::string(name);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    expect(!error, "create persistence test directory");
    return root;
}

void testSerializedPathUtf8Validation() {
    const std::vector<std::string> malformed{
        std::string(1, static_cast<char>(0x91)),
        std::string{static_cast<char>(0xD0), '\0'},
        "\xC0\xAF", "\xE0\x80\xAF", "\xF0\x80\x80\xAF",
        "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xF5\x80\x80\x80",
        "\xC2", "\xE2\x82", "\xF0\x9F\x92", "\xFF"
    };
    for (const auto& bytes : malformed) {
        nff::persistence::BinaryWriter writer;
        writer.writeString(bytes);
        nff::persistence::BinaryReader reader(writer.bytes());
        std::filesystem::path path("unchanged");
        expect(!reader.readPath(path, 1024U), "malformed UTF-8 path is rejected before native conversion");
        expect(path == "unchanged", "rejected path leaves output unchanged");
    }
    const std::vector<std::string> valid{
        "", "relative/file.txt", "\xC4\xB0stanbul/\xC3\xA7.txt",
        "\xE4\xB8\xAD/\xF0\x9F\x98\x80.txt",
        "\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80\xEF\xBF\xBF\xF0\x90\x80\x80\xF4\x8F\xBF\xBF"
    };
    for (const auto& bytes : valid) {
        nff::persistence::BinaryWriter writer;
        writer.writeString(bytes);
        nff::persistence::BinaryReader reader(writer.bytes());
        std::filesystem::path path;
        expect(reader.readPath(path, 1024U), "valid UTF-8 path is accepted");
        nff::persistence::BinaryWriter roundTrip;
        roundTrip.writePath(path);
        expect(roundTrip.bytes() == writer.bytes(), "valid UTF-8 path round trips without loss");
    }
}

void testEnvelopeChecksum() {
    const auto root = freshTempDirectory("nff-persistence-envelope");
    const auto privateRoot = root / "private";
    const auto path = privateRoot / "state.bin";
    constexpr nff::persistence::Magic magic{
        std::byte{'N'}, std::byte{'F'}, std::byte{'F'}, std::byte{'T'},
        std::byte{'E'}, std::byte{'S'}, std::byte{'T'}, std::byte{'1'}};
    const std::vector<std::byte> payload{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    expect(!nff::persistence::writeEnvelope(path, magic, 1U, payload), "write envelope");

#ifdef __linux__
    struct stat directoryStatus {};
    struct stat fileStatus {};
    expect(::stat(privateRoot.c_str(), &directoryStatus) == 0 &&
               (directoryStatus.st_mode & 0777) == 0700,
           "Linux envelope parent directory is private");
    expect(::stat(path.c_str(), &fileStatus) == 0 &&
               (fileStatus.st_mode & 0777) == 0600,
           "Linux envelope file is private");
#endif

    const auto loaded = nff::persistence::readEnvelope(path, magic, 1024U);
    expect(static_cast<bool>(loaded), "read valid envelope");
    if (loaded) {
        expect(loaded.schemaVersion == 1U, "envelope schema preserved");
        expect(loaded.payload == payload, "envelope payload preserved");
    }

    auto bytes = nff::storage::FileReader::readAll(path, 1024U);
    expect(static_cast<bool>(bytes), "read envelope for corruption");
    if (bytes && !bytes.bytes.empty()) {
        bytes.bytes.back() ^= std::byte{0x7FU};
        expect(!nff::storage::FileWriter::writeAtomically(path, bytes.bytes), "corrupt envelope");
        const auto corrupted = nff::persistence::readEnvelope(path, magic, 1024U);
        expect(!corrupted, "checksum rejects corrupted envelope");
    }

    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}

void testMetadataRoundTripAndEditTracking() {
    const auto root = freshTempDirectory("nff-metadata");
    const auto documentPath = root / "notes.txt";
    constexpr std::string_view text = "alpha beta gamma";

    nff::metadata::TextColorMap colors;
    colors.setColor(0U, 5U, 0xFFFF0000U);
    colors.setColor(6U, 10U, 0xFF00FF00U);
    expect(colors.spans().size() == 2U, "two color spans stored");

    colors.applyEdit(0U, 0U, 2U);
    expect(colors.spans().size() == 2U && colors.spans()[0].begin == 2U,
           "insert shifts color spans");
    colors.applyEdit(3U, 4U, 0U);
    expect(!colors.spans().empty(), "erase keeps unaffected color remainder");

    nff::metadata::TextColorMap savedColors;
    savedColors.setColor(0U, 5U, 0xFFFF0000U);
    savedColors.setColor(6U, 10U, 0xFF00FF00U);
    nff::metadata::MetadataStore store(root / "metadata");
    expect(!store.save(documentPath, text, savedColors), "save metadata");

    const auto loaded = store.load(documentPath, text);
    expect(static_cast<bool>(loaded), "load metadata");
    if (loaded) {
        expect(!loaded.stale, "matching text metadata is fresh");
        expect(loaded.metadata.colors.spans() == savedColors.spans(), "color spans round trip");
    }

    const auto stale = store.load(documentPath, "alpha changed gamma");
    expect(static_cast<bool>(stale) && stale.stale, "changed text marks metadata stale");

    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}

void testTextColorLengthPreservingEditPolicy() {
    nff::metadata::TextColorMap colors;
    colors.setColor(0U, 3U, 0xFFFF0000U);
    colors.setColor(4U, 7U, 0xFF0000FFU);
    const auto before = colors.spans();

    colors.applyEdit(0U, 7U, 7U,
                     nff::metadata::TextColorEditPolicy::PreserveOffsets);

    expect(colors.spans() == before,
           "length-preserving semantic replacement keeps color offsets exactly");
}

void testTextColorPreserveStatePolicyIgnoresUndoTextDelta() {
    nff::metadata::TextColorMap colors;
    colors.setColor(2U, 6U, 0xFFFF0000U);
    const auto before = colors.spans();

    colors.applyEdit(0U, 4U, 1U,
                     nff::metadata::TextColorEditPolicy::PreserveState);

    expect(colors.spans() == before,
           "undo/redo text deltas can leave metadata untouched for container restore");
}

void testTextColorRangeIntersectionDetectsMetadataAtRisk() {
    nff::metadata::TextColorMap colors;
    colors.setColor(4U, 9U, 0xFFFF0000U);
    colors.setColor(15U, 20U, 0xFF0000FFU);

    expect(!colors.intersects(0U, 4U), "range ending at color start does not intersect");
    expect(colors.intersects(3U, 5U), "range crossing color start intersects");
    expect(colors.intersects(8U, 16U), "range crossing adjacent colored spans intersects");
    expect(!colors.intersects(9U, 15U), "gap between colored spans does not intersect");
    expect(!colors.intersects(20U, 30U), "range after colors does not intersect");
}

void testSelectionAppearanceSparseOrthogonalProperties() {
    nff::metadata::TextAppearanceMap appearance;
    const auto family = appearance.internFontFamily("Consolas");
    expect(family.has_value(), "font family interning returns an id");
    if (!family) {
        return;
    }

    appearance.setForeground(0U, 12U, 0xFFCC3344U);
    expect(appearance.setFontFamily(2U, 10U, *family), "font family can be applied to selection");
    expect(appearance.setFontSize(4U, 8U, 30U), "30 pt selection size accepted");
    expect(!appearance.setFontSize(4U, 8U, 31U), "selection size above 30 pt rejected");
    expect(!appearance.setFontSize(4U, 8U, 4U), "selection size below 5 pt rejected");
    appearance.setSpoiler(6U, 12U, true);

    const auto atSeven = appearance.styleAt(7U);
    expect(atSeven.foregroundArgb == std::optional<std::uint32_t>{0xFFCC3344U},
           "foreground survives font and spoiler mutations");
    expect(atSeven.fontFamilyId == std::optional<nff::metadata::FontFamilyId>{*family},
           "font family survives other appearance mutations");
    expect(atSeven.fontSizePoints == std::optional<std::uint8_t>{static_cast<std::uint8_t>(30U)},
           "font size survives other appearance mutations");
    expect(atSeven.spoiler, "spoiler property combines with color/font/size");
    expect(appearance.hasSpoilers(), "spoiler presence cache tracks sparse spans");

    appearance.clearForeground(6U, 8U);
    const auto withoutColor = appearance.styleAt(7U);
    expect(!withoutColor.foregroundArgb.has_value(), "clearing foreground targets only foreground");
    expect(withoutColor.fontFamilyId == atSeven.fontFamilyId &&
               withoutColor.fontSizePoints == atSeven.fontSizePoints && withoutColor.spoiler,
           "clearing foreground preserves font size family and spoiler");

    appearance.reset(7U, 8U);
    expect(appearance.styleAt(7U).empty(), "reset selection appearance removes every override");
}

void testSelectionAppearanceInsertionInheritanceAndBoundaries() {
    nff::metadata::TextAppearanceMap appearance;
    appearance.setForeground(2U, 8U, 0xFF112233U);
    appearance.setSpoiler(2U, 8U, true);

    appearance.applyEdit(5U, 0U, 3U);
    const auto inserted = appearance.styleAt(6U);
    expect(inserted.foregroundArgb == std::optional<std::uint32_t>{0xFF112233U} && inserted.spoiler,
           "insertion inside one appearance span inherits that appearance");

    nff::metadata::TextAppearanceMap boundary;
    boundary.setForeground(0U, 4U, 0xFFFF0000U);
    boundary.setForeground(4U, 8U, 0xFF0000FFU);
    boundary.applyEdit(4U, 0U, 2U);
    expect(boundary.styleAt(4U).empty(),
           "insertion at mixed-style boundary inherits document defaults rather than guessing");
}

void testSelectionAppearanceSparseEditStress() {
    nff::metadata::TextAppearanceMap appearance;
    constexpr std::uint64_t spanCount = 5000U;
    for (std::uint64_t index = 0U; index < spanCount; ++index) {
        const auto begin = index * 4U;
        appearance.setForeground(begin, begin + 2U,
                                 index % 2U == 0U ? 0xFF112233U : 0xFF334455U);
    }
    expect(appearance.spans().size() == spanCount, "sparse appearance fixture keeps 5000 spans");
    appearance.applyEdit(10001U, 0U, 7U);
    expect(appearance.intersects(10000U, 10002U), "binary sparse intersection finds nearby span");
    expect(appearance.styleAt(10008U).foregroundArgb.has_value(),
           "sparse insertion shifts following appearance spans");
    const auto fragment = appearance.fragment(9990U, 10030U);
    expect(!fragment.empty(), "sparse fragment lookup returns bounded nearby spans");
    appearance.applyEdit(8000U, 400U, 13U);
    const auto& spans = appearance.spans();
    expect(std::is_sorted(spans.begin(), spans.end(), [](const auto& left, const auto& right) {
               return left.begin < right.begin || (left.begin == right.begin && left.end <= right.end);
           }), "sparse appearance edit preserves sorted span invariant");
}

void testSelectionAppearanceV2RoundTrip() {
    const auto root = freshTempDirectory("nff-appearance-v2");
    const auto documentPath = root / "appearance.txt";
    constexpr std::string_view text = "alpha beta gamma";

    nff::metadata::TextAppearanceMap appearance;
    const auto family = appearance.internFontFamily("DejaVu Sans Mono");
    expect(family.has_value(), "v2 test interns system font name");
    if (family) {
        appearance.setForeground(0U, 5U, 0xFF102030U);
        expect(appearance.setFontFamily(0U, 5U, *family), "v2 test applies font family");
        expect(appearance.setFontSize(0U, 5U, 15U), "v2 test applies font size");
        appearance.setSpoiler(6U, 10U, true);
    }

    nff::metadata::MetadataStore store(root / "metadata");
    expect(!store.save(documentPath, text, appearance), "save v2 appearance metadata");
    const auto loaded = store.load(documentPath, text);
    expect(static_cast<bool>(loaded) && !loaded.stale, "load fresh v2 appearance metadata");
    if (loaded) {
        expect(loaded.metadata.appearance.spans() == appearance.spans(), "appearance spans round trip");
        expect(loaded.metadata.appearance.fontFamilies() == appearance.fontFamilies(), "interned fonts round trip");
    }

    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}

void testSelectionAppearanceV1ColorMigration() {
    const auto root = freshTempDirectory("nff-appearance-v1-migration");
    const auto documentPath = root / "legacy.txt";
    constexpr std::string_view text = "legacy colors";
    constexpr nff::persistence::Magic metadataMagic{
        std::byte{'N'}, std::byte{'F'}, std::byte{'F'}, std::byte{'M'},
        std::byte{'E'}, std::byte{'T'}, std::byte{'0'}, std::byte{'1'}};

    nff::metadata::MetadataStore store(root / "metadata");
    std::error_code directoryError;
    std::filesystem::create_directories(store.root(), directoryError);
    expect(!directoryError, "create v1 metadata directory");

    nff::persistence::BinaryWriter writer;
    writer.writePath(documentPath);
    writer.writeU64(nff::persistence::hash64(text));
    writer.writeU64(static_cast<std::uint64_t>(text.size()));
    writer.writeU64(1U);
    writer.writeU64(0U);
    writer.writeU64(6U);
    writer.writeU32(0xFF336699U);
    expect(!nff::persistence::writeEnvelope(store.metadataPath(documentPath), metadataMagic, 1U,
                                            writer.bytes()),
           "write legacy v1 color sidecar");

    const auto loaded = store.load(documentPath, text);
    expect(static_cast<bool>(loaded) && !loaded.stale, "load legacy v1 color sidecar");
    if (loaded) {
        const auto style = loaded.metadata.appearance.styleAt(2U);
        expect(style.foregroundArgb == std::optional<std::uint32_t>{0xFF336699U},
               "v1 foreground migrates into v2 appearance model");
        expect(!style.fontFamilyId.has_value() && !style.fontSizePoints.has_value() && !style.spoiler,
               "v1 migration adds no unrelated appearance properties");
    }

    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}

void testSelectionAppearanceRejectsCorruptV2FontId() {
    const auto root = freshTempDirectory("nff-appearance-v2-bad-font-id");
    const auto documentPath = root / "bad-font.txt";
    constexpr std::string_view text = "alpha";
    constexpr nff::persistence::Magic metadataMagic{
        std::byte{'N'}, std::byte{'F'}, std::byte{'F'}, std::byte{'M'},
        std::byte{'E'}, std::byte{'T'}, std::byte{'0'}, std::byte{'1'}};

    nff::metadata::MetadataStore store(root / "metadata");
    std::error_code directoryError;
    std::filesystem::create_directories(store.root(), directoryError);
    expect(!directoryError, "create corrupt v2 metadata directory");

    nff::persistence::BinaryWriter writer;
    writer.writePath(documentPath);
    writer.writeU64(nff::persistence::hash64(text));
    writer.writeU64(static_cast<std::uint64_t>(text.size()));
    writer.writeU64(1U);
    writer.writeString("Consolas");
    writer.writeU64(1U);
    writer.writeU64(0U);
    writer.writeU64(5U);
    writer.writeU8(0x02U);
    writer.writeU32(2U);
    expect(!nff::persistence::writeEnvelope(store.metadataPath(documentPath), metadataMagic, 2U,
                                            writer.bytes()),
           "write corrupt v2 font id sidecar");

    const auto loaded = store.load(documentPath, text);
    expect(!loaded && loaded.error == std::make_error_code(std::errc::illegal_byte_sequence),
           "out-of-range v2 font id is rejected safely");

    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}

void testSelectionAppearanceSidecarPreservesPlainTextBytes() {
    const auto root = freshTempDirectory("nff-appearance-byte-fidelity");
    const auto documentPath = root / "bytes.txt";
    constexpr std::string_view text = "alpha\r\nbeta\r\n";
    const auto sourceBytes = std::as_bytes(std::span{text.data(), text.size()});
    expect(!nff::storage::FileWriter::writeAtomically(documentPath, sourceBytes),
           "write source text before appearance metadata");

    nff::metadata::TextAppearanceMap appearance;
    const auto family = appearance.internFontFamily("Consolas");
    appearance.setForeground(0U, 5U, 0xFFABCDEFU);
    if (family) {
        expect(appearance.setFontFamily(0U, 5U, *family), "byte fidelity test applies font family");
    }
    expect(appearance.setFontSize(0U, 5U, 18U), "byte fidelity test applies font size");
    appearance.setSpoiler(7U, 11U, true);

    nff::metadata::MetadataStore store(root / "metadata");
    expect(!store.save(documentPath, text, appearance), "save appearance sidecar without text mutation");

    const auto after = nff::storage::FileReader::readAll(documentPath, 1024U);
    expect(static_cast<bool>(after), "read source text after appearance metadata save");
    if (after) {
        expect(after.bytes.size() == sourceBytes.size() &&
                   std::equal(after.bytes.begin(), after.bytes.end(), sourceBytes.begin(), sourceBytes.end()),
               "appearance metadata does not embed or alter plain-text bytes");
    }

    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}

void testSettingsRoundTrip() {
    const auto root = freshTempDirectory("nff-settings");
    const auto path = root / "settings.nff";

    nff::settings::AppSettings settings;
    settings.theme = nff::settings::ThemePreference::Dark;
    settings.accent = nff::settings::AccentPreference::Teal;
    settings.density = nff::settings::UiDensity::Compact;
    settings.fontFamily = "JetBrains Mono";
    settings.fontPointSize = 13.5;
    settings.wordWrap = false;
    settings.showLineNumbers = true;
    settings.tabsEnabled = false;
    settings.sidebarVisible = false;
    settings.sidebarRootUtf8 = "/tmp/notepadFasaFiso-workspace";
    settings.viewerPerformance = nff::viewer::PerformanceProfile::MemorySaver;
    settings.inspectOptions.viewerSizeThreshold = 32ULL * 1024ULL * 1024ULL;
    settings.autoSave.saveRealFiles = true;
    settings.autoSave.saveDelay = std::chrono::milliseconds(2700);
    settings.highlightUrls = true;

    expect(!nff::settings::SettingsStore::save(path, settings), "save settings");
    const auto loaded = nff::settings::SettingsStore::load(path);
    expect(static_cast<bool>(loaded), "load settings");
    if (loaded) {
        expect(loaded.settings.theme == settings.theme, "theme round trip");
        expect(loaded.settings.accent == settings.accent, "accent round trip");
        expect(loaded.settings.density == settings.density, "density round trip");
        expect(loaded.settings.fontFamily == settings.fontFamily, "font round trip");
        expect(loaded.settings.fontPointSize == settings.fontPointSize, "font size round trip");
        expect(loaded.settings.sidebarRootUtf8 == settings.sidebarRootUtf8,
               "sidebar root round trip");
        expect(loaded.settings.highlightUrls == settings.highlightUrls,
               "link detection opt-in round trip");
        expect(loaded.settings.viewerPerformance == settings.viewerPerformance,
               "viewer profile round trip");
        expect(loaded.settings.autoSave.saveDelay == settings.autoSave.saveDelay,
               "autosave delay round trip");
    }

    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}

void testSettingsV1MigrationDefaults() {
    const auto root = freshTempDirectory("nff-settings-v1");
    const auto path = root / "settings.nff";
    constexpr nff::persistence::Magic magic{
        std::byte{'N'}, std::byte{'F'}, std::byte{'F'}, std::byte{'S'},
        std::byte{'E'}, std::byte{'T'}, std::byte{'0'}, std::byte{'1'}};

    nff::settings::AppSettings legacy;
    legacy.theme = nff::settings::ThemePreference::Light;
    legacy.fontFamily = "Consolas";
    legacy.fontPointSize = 11.0;
    legacy.highlightUrls = true;

    nff::persistence::BinaryWriter writer;
    writer.writeU8(static_cast<std::uint8_t>(legacy.theme));
    writer.writeString(legacy.fontFamily);
    writer.writeDouble(legacy.fontPointSize);
    writer.writeBool(legacy.wordWrap);
    writer.writeBool(legacy.showLineNumbers);
    writer.writeBool(legacy.highlightUrls);
    writer.writeBool(legacy.tabsEnabled);
    writer.writeBool(legacy.sidebarVisible);
    writer.writeBool(legacy.restorePreviousSession);
    writer.writeU8(static_cast<std::uint8_t>(legacy.viewerPerformance));
    writer.writeU64(static_cast<std::uint64_t>(legacy.inspectOptions.viewerSizeThreshold));
    writer.writeU64(static_cast<std::uint64_t>(legacy.inspectOptions.longLineThreshold));
    writer.writeU64(static_cast<std::uint64_t>(legacy.inspectOptions.sampleBytes));
    writer.writeBool(legacy.autoSave.recoveryEnabled);
    writer.writeU64(static_cast<std::uint64_t>(legacy.autoSave.recoveryDelay.count()));
    writer.writeBool(legacy.autoSave.saveRealFiles);
    writer.writeU64(static_cast<std::uint64_t>(legacy.autoSave.saveDelay.count()));
    writer.writeBool(legacy.autoSave.saveOnFocusLoss);

    expect(!nff::persistence::writeEnvelope(path, magic, 1U, writer.bytes()),
           "write legacy settings");
    const auto loaded = nff::settings::SettingsStore::load(path);
    expect(static_cast<bool>(loaded), "load legacy settings");
    if (loaded) {
        expect(loaded.settings.theme == legacy.theme, "legacy theme preserved");
        expect(loaded.settings.fontFamily == legacy.fontFamily, "legacy font preserved");
        expect(loaded.settings.accent == nff::settings::AccentPreference::Violet,
               "legacy settings receive default accent");
        expect(loaded.settings.density == nff::settings::UiDensity::Comfortable,
               "legacy settings receive default density");
        expect(loaded.settings.sidebarRootUtf8.empty(),
               "legacy settings receive empty sidebar root");
        expect(!loaded.settings.highlightUrls,
               "legacy dormant link detection migrates disabled");
    }

    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}

void testSettingsV2MigrationDefaults() {
    const auto root = freshTempDirectory("nff-settings-v2");
    const auto path = root / "settings.nff";
    constexpr nff::persistence::Magic magic{
        std::byte{'N'}, std::byte{'F'}, std::byte{'F'}, std::byte{'S'},
        std::byte{'E'}, std::byte{'T'}, std::byte{'0'}, std::byte{'1'}};

    nff::settings::AppSettings legacy;
    legacy.theme = nff::settings::ThemePreference::Dark;
    legacy.accent = nff::settings::AccentPreference::Amber;
    legacy.density = nff::settings::UiDensity::Compact;
    legacy.fontFamily = "JetBrains Mono";
    legacy.highlightUrls = true;

    nff::persistence::BinaryWriter writer;
    writer.writeU8(static_cast<std::uint8_t>(legacy.theme));
    writer.writeU8(static_cast<std::uint8_t>(legacy.accent));
    writer.writeU8(static_cast<std::uint8_t>(legacy.density));
    writer.writeString(legacy.fontFamily);
    writer.writeDouble(legacy.fontPointSize);
    writer.writeBool(legacy.wordWrap);
    writer.writeBool(legacy.showLineNumbers);
    writer.writeBool(legacy.highlightUrls);
    writer.writeBool(legacy.tabsEnabled);
    writer.writeBool(legacy.sidebarVisible);
    writer.writeBool(legacy.restorePreviousSession);
    writer.writeU8(static_cast<std::uint8_t>(legacy.viewerPerformance));
    writer.writeU64(static_cast<std::uint64_t>(legacy.inspectOptions.viewerSizeThreshold));
    writer.writeU64(static_cast<std::uint64_t>(legacy.inspectOptions.longLineThreshold));
    writer.writeU64(static_cast<std::uint64_t>(legacy.inspectOptions.sampleBytes));
    writer.writeBool(legacy.autoSave.recoveryEnabled);
    writer.writeU64(static_cast<std::uint64_t>(legacy.autoSave.recoveryDelay.count()));
    writer.writeBool(legacy.autoSave.saveRealFiles);
    writer.writeU64(static_cast<std::uint64_t>(legacy.autoSave.saveDelay.count()));
    writer.writeBool(legacy.autoSave.saveOnFocusLoss);

    expect(!nff::persistence::writeEnvelope(path, magic, 2U, writer.bytes()),
           "write v2 settings");
    const auto loaded = nff::settings::SettingsStore::load(path);
    expect(static_cast<bool>(loaded), "load v2 settings");
    if (loaded) {
        expect(loaded.settings.accent == legacy.accent, "v2 accent preserved");
        expect(loaded.settings.density == legacy.density, "v2 density preserved");
        expect(loaded.settings.sidebarRootUtf8.empty(),
               "v2 settings receive empty sidebar root");
        expect(!loaded.settings.highlightUrls,
               "v2 dormant link detection migrates disabled");
    }

    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}

void testSettingsV3MigrationDefaults() {
    const auto root = freshTempDirectory("nff-settings-v3");
    const auto path = root / "settings.nff";
    constexpr nff::persistence::Magic magic{
        std::byte{'N'}, std::byte{'F'}, std::byte{'F'}, std::byte{'S'},
        std::byte{'E'}, std::byte{'T'}, std::byte{'0'}, std::byte{'1'}};

    nff::settings::AppSettings legacy;
    legacy.sidebarRootUtf8 = "/tmp/legacy-workspace";
    legacy.highlightUrls = true;

    nff::persistence::BinaryWriter writer;
    writer.writeU8(static_cast<std::uint8_t>(legacy.theme));
    writer.writeU8(static_cast<std::uint8_t>(legacy.accent));
    writer.writeU8(static_cast<std::uint8_t>(legacy.density));
    writer.writeString(legacy.fontFamily);
    writer.writeDouble(legacy.fontPointSize);
    writer.writeBool(legacy.wordWrap);
    writer.writeBool(legacy.showLineNumbers);
    writer.writeBool(legacy.highlightUrls);
    writer.writeBool(legacy.tabsEnabled);
    writer.writeBool(legacy.sidebarVisible);
    writer.writeBool(legacy.restorePreviousSession);
    writer.writeU8(static_cast<std::uint8_t>(legacy.viewerPerformance));
    writer.writeU64(static_cast<std::uint64_t>(legacy.inspectOptions.viewerSizeThreshold));
    writer.writeU64(static_cast<std::uint64_t>(legacy.inspectOptions.longLineThreshold));
    writer.writeU64(static_cast<std::uint64_t>(legacy.inspectOptions.sampleBytes));
    writer.writeBool(legacy.autoSave.recoveryEnabled);
    writer.writeU64(static_cast<std::uint64_t>(legacy.autoSave.recoveryDelay.count()));
    writer.writeBool(legacy.autoSave.saveRealFiles);
    writer.writeU64(static_cast<std::uint64_t>(legacy.autoSave.saveDelay.count()));
    writer.writeBool(legacy.autoSave.saveOnFocusLoss);
    writer.writeString(legacy.sidebarRootUtf8);

    expect(!nff::persistence::writeEnvelope(path, magic, 3U, writer.bytes()),
           "write v3 settings");
    const auto loaded = nff::settings::SettingsStore::load(path);
    expect(static_cast<bool>(loaded), "load v3 settings");
    if (loaded) {
        expect(loaded.settings.sidebarRootUtf8 == legacy.sidebarRootUtf8,
               "v3 sidebar root preserved");
        expect(!loaded.settings.highlightUrls,
               "v3 dormant link detection migrates disabled");
    }

    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}

void testSettingsV4MigrationDefaults() {
    const auto root = freshTempDirectory("nff-settings-v4");
    const auto path = root / "settings.nff";
    constexpr nff::persistence::Magic magic{
        std::byte{'N'}, std::byte{'F'}, std::byte{'F'}, std::byte{'S'},
        std::byte{'E'}, std::byte{'T'}, std::byte{'0'}, std::byte{'1'}};

    nff::settings::AppSettings legacy;
    legacy.sidebarRootUtf8 = "/tmp/v4-workspace";
    legacy.highlightUrls = true;

    nff::persistence::BinaryWriter writer;
    writer.writeU8(static_cast<std::uint8_t>(legacy.theme));
    writer.writeU8(static_cast<std::uint8_t>(legacy.accent));
    writer.writeU8(static_cast<std::uint8_t>(legacy.density));
    writer.writeString(legacy.fontFamily);
    writer.writeDouble(legacy.fontPointSize);
    writer.writeBool(legacy.wordWrap);
    writer.writeBool(legacy.showLineNumbers);
    writer.writeBool(legacy.highlightUrls);
    writer.writeBool(legacy.tabsEnabled);
    writer.writeBool(legacy.sidebarVisible);
    writer.writeBool(legacy.restorePreviousSession);
    writer.writeU8(static_cast<std::uint8_t>(legacy.viewerPerformance));
    writer.writeU64(static_cast<std::uint64_t>(legacy.inspectOptions.viewerSizeThreshold));
    writer.writeU64(static_cast<std::uint64_t>(legacy.inspectOptions.longLineThreshold));
    writer.writeU64(static_cast<std::uint64_t>(legacy.inspectOptions.sampleBytes));
    writer.writeBool(legacy.autoSave.recoveryEnabled);
    writer.writeU64(static_cast<std::uint64_t>(legacy.autoSave.recoveryDelay.count()));
    writer.writeBool(legacy.autoSave.saveRealFiles);
    writer.writeU64(static_cast<std::uint64_t>(legacy.autoSave.saveDelay.count()));
    writer.writeBool(legacy.autoSave.saveOnFocusLoss);
    writer.writeString(legacy.sidebarRootUtf8);
    writer.writeBool(false);

    expect(!nff::persistence::writeEnvelope(path, magic, 4U, writer.bytes()),
           "write v4 settings");
    const auto loaded = nff::settings::SettingsStore::load(path);
    expect(static_cast<bool>(loaded), "load v4 settings");
    if (loaded) {
        expect(loaded.settings.sidebarRootUtf8 == legacy.sidebarRootUtf8,
               "v4 sidebar root preserved");
        expect(!loaded.settings.highlightUrls,
               "v4 dormant link detection migrates disabled");
    }

    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}

void testSessionWorkspaceRoundTrip() {
    const auto root = freshTempDirectory("nff-session");
    const auto path = root / "session.nff";

    nff::core::DocumentManager documents;
    const auto firstDocument = documents.createUntitled();
    const auto secondDocument = documents.createUntitled();
    auto* first = documents.get(firstDocument);
    auto* second = documents.get(secondDocument);
    expect(first != nullptr && second != nullptr, "session documents created");
    if (first == nullptr || second == nullptr) {
        return;
    }
    first->replaceText("first");
    second->replaceText("second");

    nff::workspace::WorkspaceModel workspace;
    const auto firstView = workspace.openView(firstDocument, workspace.primaryPane());
    const auto split = workspace.splitPane(workspace.primaryPane(),
                                           nff::workspace::SplitOrientation::Horizontal,
                                           nff::workspace::SplitPlacement::After, 0.37);
    expect(static_cast<bool>(split), "create session split");
    const auto secondView = workspace.openView(secondDocument, split.pane);
    expect(static_cast<bool>(firstView) && static_cast<bool>(secondView), "open session views");
    if (auto* view = workspace.view(firstView)) {
        view->caretOffset = 4U;
        view->anchorOffset = 1U;
        view->firstVisibleLine = 12U;
        view->wordWrapOverride = false;
        view->lineNumbersOverride = true;
        view->fontFamilyOverride = "Consolas";
        view->fontPointSizeOverride = 15.0;
    }
    expect(workspace.setActivePane(split.pane), "set active pane");

    auto state = nff::session::SessionStore::capture(documents, workspace);
    expect(!nff::session::SessionStore::save(path, state), "save session");

    auto loaded = nff::session::SessionStore::load(path);
    expect(static_cast<bool>(loaded), "load session");
    if (loaded) {
        expect(loaded.state.documents.size() == 2U, "session documents round trip");
        nff::workspace::WorkspaceModel restored;
        expect(restored.restore(loaded.state.workspace), "restore workspace snapshot");
        expect(restored.paneCount() == 2U, "restored pane count");
        expect(restored.viewCount() == 2U, "restored view count");
        expect(restored.activePane() == split.pane, "active pane restored");
        const auto* restoredView = restored.view(firstView);
        expect(restoredView != nullptr && restoredView->caretOffset == 4U &&
                   restoredView->firstVisibleLine == 12U,
               "view cursor state restored");
        expect(restoredView != nullptr && restoredView->wordWrapOverride == false &&
                   restoredView->lineNumbersOverride == true,
               "per-view wrap and line-number overrides restored");
        expect(restoredView != nullptr && restoredView->fontFamilyOverride == "Consolas" &&
                   restoredView->fontPointSizeOverride == 15.0,
               "per-view font overrides restored");
    }

    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}

}

void testSessionDeterministicCorruption() {
    const auto root = freshTempDirectory("nff-session-corruption");
    const auto path = root / "session.nff";
    constexpr nff::persistence::Magic magic{
        std::byte{'N'}, std::byte{'F'}, std::byte{'F'}, std::byte{'S'},
        std::byte{'E'}, std::byte{'S'}, std::byte{'0'}, std::byte{'1'}};
    nff::core::DocumentManager documents;
    const auto id = documents.createUntitled();
    nff::workspace::WorkspaceModel workspace;
    static_cast<void>(workspace.openView(id, workspace.primaryPane()));
    const auto split = workspace.splitPane(workspace.primaryPane(), nff::workspace::SplitOrientation::Vertical);
    static_cast<void>(workspace.openView(id, split.pane));
    const auto state = nff::session::SessionStore::capture(documents, workspace);
    expect(!nff::session::SessionStore::save(path, state), "write corruption seed");
    const auto envelope = nff::persistence::readEnvelope(path, magic, 1024U * 1024U);
    expect(static_cast<bool>(envelope), "read corruption seed payload");
    if (!envelope) return;
    for (std::size_t size = 0; size < envelope.payload.size(); ++size) {
        expect(!nff::persistence::writeEnvelope(path, magic, envelope.schemaVersion,
            std::span<const std::byte>(envelope.payload).first(size)), "write checksummed truncated payload");
        expect(!nff::session::SessionStore::load(path), "every truncated payload rejected after checksum verification");
    }
    std::mt19937 random(0x5e5510U);
    std::size_t rejected = 0;
    for (unsigned int i = 0; i < 512; ++i) {
        auto payload = envelope.payload;
        const auto changes = 1U + random() % 4U;
        for (unsigned int j = 0; j < changes; ++j) payload[random() % payload.size()] ^= static_cast<std::byte>(1U + random() % 255U);
        expect(!nff::persistence::writeEnvelope(path, magic, envelope.schemaVersion, payload), "write checksummed corrupt payload");
        auto loaded = nff::session::SessionStore::load(path);
        if (!loaded) { ++rejected; continue; }
        nff::workspace::WorkspaceModel restored;
        expect(restored.restore(loaded.state.workspace), "accepted mutation obeys workspace invariants");
        expect(!nff::session::SessionStore::save(path, loaded.state), "accepted mutation can be serialized");
        expect(static_cast<bool>(nff::session::SessionStore::load(path)), "accepted mutation round trips");
    }
    expect(rejected != 0, "corruption corpus exercises rejection");
    auto trailing = envelope.payload;
    trailing.push_back(std::byte{0});
    expect(!nff::persistence::writeEnvelope(path, magic, envelope.schemaVersion, trailing), "write trailing data");
    expect(!nff::session::SessionStore::load(path), "trailing data rejected");
    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}

int main() {
    testSerializedPathUtf8Validation();
    testSessionDeterministicCorruption();
    testEnvelopeChecksum();
    testMetadataRoundTripAndEditTracking();
    testTextColorLengthPreservingEditPolicy();
    testTextColorPreserveStatePolicyIgnoresUndoTextDelta();
    testTextColorRangeIntersectionDetectsMetadataAtRisk();
    testSelectionAppearanceSparseOrthogonalProperties();
    testSelectionAppearanceInsertionInheritanceAndBoundaries();
    testSelectionAppearanceSparseEditStress();
    testSelectionAppearanceV2RoundTrip();
    testSelectionAppearanceV1ColorMigration();
    testSelectionAppearanceRejectsCorruptV2FontId();
    testSelectionAppearanceSidecarPreservesPlainTextBytes();
    testSettingsRoundTrip();
    testSettingsV1MigrationDefaults();
    testSettingsV2MigrationDefaults();
    testSettingsV3MigrationDefaults();
    testSettingsV4MigrationDefaults();
    testSessionWorkspaceRoundTrip();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all persistence tests passed\n";
    return 0;
}
