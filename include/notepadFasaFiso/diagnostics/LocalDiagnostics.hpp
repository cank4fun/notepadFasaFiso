#pragma once

#include <filesystem>
#include <string_view>
#include <system_error>

namespace nff::diagnostics {

struct DiagnosticsPaths final {
    std::filesystem::path directory;
    std::filesystem::path exceptionFile;
    std::filesystem::path crashDirectory;

    [[nodiscard]] static DiagnosticsPaths under(const std::filesystem::path& applicationRoot);
};

struct DiagnosticsInitResult final {
    DiagnosticsPaths paths;
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

[[nodiscard]] DiagnosticsInitResult initialize(
    const std::filesystem::path& applicationRoot) noexcept;
void shutdown() noexcept;

void writeExceptionReport(std::string_view category,
                          std::string_view message = {}) noexcept;

void installPlatformCrashHandler(const DiagnosticsPaths& paths) noexcept;
void uninstallPlatformCrashHandler() noexcept;

}
