#include "notepadFasaFiso/diagnostics/LocalDiagnostics.hpp"

#include "notepadFasaFiso/platform/Platform.hpp"
#include "notepadFasaFiso/storage/AtomicSave.hpp"

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <span>
#include <string>

namespace nff::diagnostics {
namespace {

constexpr std::size_t maxExceptionCategoryBytes = 128U;
constexpr std::size_t maxExceptionMessageBytes = 4096U;

struct DiagnosticsState final {
    std::mutex mutex;
    DiagnosticsPaths paths;
    bool initialized{false};
};

DiagnosticsState& state() {
    static DiagnosticsState instance;
    return instance;
}

[[nodiscard]] std::string oneLineBounded(const std::string_view value,
                                         const std::size_t limit) {
    const auto count = std::min(value.size(), limit);
    std::string result;
    result.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const char ch = value[index];
        const auto byte = static_cast<unsigned char>(ch);
        if (ch == '\r' || ch == '\n' || ch == '\t' || byte < 0x20U) {
            result.push_back(' ');
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

}

DiagnosticsPaths DiagnosticsPaths::under(const std::filesystem::path& applicationRoot) {
    DiagnosticsPaths result;
    if (applicationRoot.empty()) {
        return result;
    }
    result.directory = applicationRoot / "diagnostics";
    result.exceptionFile = result.directory / "exception-last.txt";
    result.crashDirectory = result.directory / "crashes";
    return result;
}

DiagnosticsInitResult initialize(const std::filesystem::path& applicationRoot) noexcept {
    DiagnosticsInitResult result;
    result.paths = DiagnosticsPaths::under(applicationRoot);
    if (result.paths.directory.empty()) {
        result.error = std::make_error_code(std::errc::invalid_argument);
        return result;
    }

    if (const auto rootError = platform::ensurePrivateDirectory(applicationRoot)) {
        result.error = rootError;
        return result;
    }
    if (const auto directoryError = platform::ensurePrivateDirectory(result.paths.directory)) {
        result.error = directoryError;
        return result;
    }
    if (const auto crashDirectoryError =
            platform::ensurePrivateDirectory(result.paths.crashDirectory)) {
        result.error = crashDirectoryError;
        return result;
    }

    auto& current = state();
    std::lock_guard lock(current.mutex);
    current.paths = result.paths;
    current.initialized = true;
    return result;
}

void shutdown() noexcept {
    uninstallPlatformCrashHandler();
    auto& current = state();
    std::lock_guard lock(current.mutex);
    current.paths = {};
    current.initialized = false;
}

void writeExceptionReport(const std::string_view category,
                          const std::string_view message) noexcept {
    try {
        std::filesystem::path exceptionFile;
        {
            auto& current = state();
            std::lock_guard lock(current.mutex);
            if (!current.initialized || current.paths.exceptionFile.empty()) {
                return;
            }
            exceptionFile = current.paths.exceptionFile;
        }

        const auto safeCategory = oneLineBounded(category, maxExceptionCategoryBytes);
        const auto safeMessage = oneLineBounded(message, maxExceptionMessageBytes);
        std::string report;
        report.reserve(160U + safeCategory.size() + safeMessage.size());
        report += "notepadFasaFiso exception report\ncategory=";
        report += safeCategory;
        report += "\nmessage=";
        report += safeMessage;
        report += "\nnote=Document contents are not intentionally included in diagnostics.\n";

        const auto bytes = std::as_bytes(std::span(report.data(), report.size()));
        if (storage::AtomicSave::write(exceptionFile, bytes)) {
            return;
        }
        if (const auto fileError = platform::hardenPrivateFile(exceptionFile)) {
            std::error_code cleanupError;
            std::filesystem::remove(exceptionFile, cleanupError);
        }
    } catch (...) {
        // pls dont die twice
    }
}

}
