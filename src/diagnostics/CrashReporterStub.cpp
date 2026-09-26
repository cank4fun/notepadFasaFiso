#include "notepadFasaFiso/diagnostics/LocalDiagnostics.hpp"

#if defined(__linux__)

#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <limits.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>

namespace nff::diagnostics {
namespace {

constexpr std::array<int, 5> fatalSignals{SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV};

struct CrashReporterState final {
    std::array<char, PATH_MAX> reportPrefix{};
    std::size_t reportPrefixLength{0U};
    std::array<struct sigaction, fatalSignals.size()> previous{};
    bool installed{false};
};

CrashReporterState& state() noexcept {
    static CrashReporterState instance;
    return instance;
}

[[nodiscard]] std::size_t appendUnsigned(char* output,
                                         const std::size_t capacity,
                                         std::uint64_t value) noexcept {
    if (capacity == 0U) {
        return 0U;
    }
    std::array<char, 32> reverse{};
    std::size_t count = 0U;
    do {
        reverse[count++] = static_cast<char>('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < reverse.size());
    if (count > capacity) {
        return 0U;
    }
    for (std::size_t index = 0U; index < count; ++index) {
        output[index] = reverse[count - index - 1U];
    }
    return count;
}

[[nodiscard]] std::size_t appendLiteral(char* output,
                                        const std::size_t capacity,
                                        const char* literal,
                                        const std::size_t length) noexcept {
    if (length > capacity) {
        return 0U;
    }
    for (std::size_t index = 0U; index < length; ++index) {
        output[index] = literal[index];
    }
    return length;
}

[[nodiscard]] bool preparePrefix(const std::filesystem::path& crashDirectory) noexcept {
    try {
        const auto utf8 = crashDirectory.generic_u8string();
        const auto& current = state();
        if (utf8.empty() || utf8.size() + 32U >= current.reportPrefix.size()) {
            return false;
        }

        auto& mutableState = state();
        std::size_t length = 0U;
        for (const char8_t byte : utf8) {
            mutableState.reportPrefix[length++] = static_cast<char>(byte);
        }
        if (length != 0U && mutableState.reportPrefix[length - 1U] != '/') {
            mutableState.reportPrefix[length++] = '/';
        }
        constexpr char stem[] = "crash-p";
        for (std::size_t index = 0U; index + 1U < sizeof(stem); ++index) {
            mutableState.reportPrefix[length++] = stem[index];
        }
        const auto pidLength = appendUnsigned(mutableState.reportPrefix.data() + length,
                                              mutableState.reportPrefix.size() - length,
                                              static_cast<std::uint64_t>(::getpid()));
        if (pidLength == 0U) {
            return false;
        }
        length += pidLength;
        constexpr char suffix[] = "-s";
        for (std::size_t index = 0U; index + 1U < sizeof(suffix); ++index) {
            mutableState.reportPrefix[length++] = suffix[index];
        }
        mutableState.reportPrefixLength = length;
        return true;
    } catch (...) {
        return false;
    }
}

void crashSignalHandler(const int signalNumber) noexcept {
    const auto& current = state();
    std::array<char, PATH_MAX> path{};
    std::size_t pathLength = current.reportPrefixLength;
    if (pathLength == 0U || pathLength >= path.size()) {
        ::_exit(128 + signalNumber);
    }
    for (std::size_t index = 0U; index < pathLength; ++index) {
        path[index] = current.reportPrefix[index];
    }
    const auto signalLength = appendUnsigned(path.data() + pathLength,
                                             path.size() - pathLength,
                                             static_cast<std::uint64_t>(signalNumber));
    if (signalLength == 0U) {
        ::_exit(128 + signalNumber);
    }
    pathLength += signalLength;
    constexpr char extension[] = ".txt";
    const auto extensionLength = appendLiteral(path.data() + pathLength,
                                               path.size() - pathLength,
                                               extension,
                                               sizeof(extension) - 1U);
    if (extensionLength == 0U || pathLength + extensionLength >= path.size()) {
        ::_exit(128 + signalNumber);
    }
    pathLength += extensionLength;
    path[pathLength] = '\0';

    const int file = ::open(path.data(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (file >= 0) {
        std::array<char, 256> report{};
        std::size_t length = 0U;
        constexpr char header[] = "notepadFasaFiso crash report\nplatform=linux\nsignal=";
        length += appendLiteral(report.data() + length,
                                report.size() - length,
                                header,
                                sizeof(header) - 1U);
        length += appendUnsigned(report.data() + length,
                                 report.size() - length,
                                 static_cast<std::uint64_t>(signalNumber));
        constexpr char pidLabel[] = "\npid=";
        length += appendLiteral(report.data() + length,
                                report.size() - length,
                                pidLabel,
                                sizeof(pidLabel) - 1U);
        length += appendUnsigned(report.data() + length,
                                 report.size() - length,
                                 static_cast<std::uint64_t>(::getpid()));
        constexpr char note[] = "\nnote=Document contents are not included in diagnostics.\n";
        length += appendLiteral(report.data() + length,
                                report.size() - length,
                                note,
                                sizeof(note) - 1U);
        if (length != 0U) {
            std::size_t written = 0U;
            while (written < length) {
                const auto result = ::write(file,
                                            report.data() + written,
                                            length - written);
                if (result > 0) {
                    written += static_cast<std::size_t>(result);
                    continue;
                }
                if (result < 0 && errno == EINTR) {
                    continue;
                }
                break;
            }
        }
        static_cast<void>(::fsync(file));
        static_cast<void>(::close(file));
    }

    ::_exit(128 + signalNumber);
}

}

void installPlatformCrashHandler(const DiagnosticsPaths& paths) noexcept {
    uninstallPlatformCrashHandler();
    auto& current = state();
    if (!preparePrefix(paths.crashDirectory)) {
        current = {};
        return;
    }

    struct sigaction action {};
    action.sa_handler = &crashSignalHandler;
    ::sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    std::size_t installedCount = 0U;
    for (std::size_t index = 0U; index < fatalSignals.size(); ++index) {
        if (::sigaction(fatalSignals[index], &action, &current.previous[index]) != 0) {
            for (std::size_t rollback = 0U; rollback < installedCount; ++rollback) {
                static_cast<void>(
                    ::sigaction(fatalSignals[rollback], &current.previous[rollback], nullptr));
            }
            current = {};
            return;
        }
        ++installedCount;
    }
    current.installed = true;
}

void uninstallPlatformCrashHandler() noexcept {
    auto& current = state();
    if (current.installed) {
        for (std::size_t index = 0U; index < fatalSignals.size(); ++index) {
            static_cast<void>(::sigaction(fatalSignals[index], &current.previous[index], nullptr));
        }
    }
    current = {};
}

}

#else

namespace nff::diagnostics {
void installPlatformCrashHandler(const DiagnosticsPaths&) noexcept {}
void uninstallPlatformCrashHandler() noexcept {}
}

#endif
