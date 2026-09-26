#include "notepadFasaFiso/persistence/BinaryCodec.hpp"
#include "notepadFasaFiso/session/SessionStore.hpp"
#include "notepadFasaFiso/settings/AppSettings.hpp"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string_view>

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
    expect(!error, "create security hardening test directory");
    return root;
}

void testSettingsRejectExcessiveInspectionAllocations() {
    nff::settings::AppSettings settings;
    constexpr std::size_t maximumInspectionBytes = 64U * 1024U * 1024U;

    settings.inspectOptions.sampleBytes = maximumInspectionBytes + 1U;
    expect(!nff::settings::validate(settings),
           "settings reject an excessive inspection sample allocation");

    settings = {};
    settings.inspectOptions.longLineThreshold = maximumInspectionBytes + 1U;
    expect(!nff::settings::validate(settings),
           "settings reject an excessive long-line inspection allocation");
}

void testSessionRejectsImpossibleDocumentCountBeforeAllocation() {
    const auto root = freshTempDirectory("nff-security-session-count");
    const auto path = root / "session.nff";
    constexpr nff::persistence::Magic sessionMagic{
        std::byte{'N'}, std::byte{'F'}, std::byte{'F'}, std::byte{'S'},
        std::byte{'E'}, std::byte{'S'}, std::byte{'0'}, std::byte{'1'}};

    nff::persistence::BinaryWriter writer;
    writer.writeU64(1024U);
    expect(!nff::persistence::writeEnvelope(path,
                                            sessionMagic,
                                            nff::session::SessionStore::currentSchemaVersion,
                                            writer.bytes()),
           "write malformed session count fixture");

    const auto loaded = nff::session::SessionStore::load(path);
    expect(!loaded, "impossible session count is rejected");
    expect(loaded.error == std::make_error_code(std::errc::value_too_large),
           "impossible session count is rejected before allocation");

    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}

}

int main() {
    testSettingsRejectExcessiveInspectionAllocations();
    testSessionRejectsImpossibleDocumentCountBeforeAllocation();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "all security hardening tests passed\n";
    return 0;
}
