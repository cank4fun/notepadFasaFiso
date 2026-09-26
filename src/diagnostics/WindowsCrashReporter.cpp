#include "notepadFasaFiso/diagnostics/LocalDiagnostics.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>

#include <array>
#include <cstdio>
#include <cwchar>
#include <filesystem>

namespace nff::diagnostics {
namespace {

struct CrashReporterState final {
    std::array<wchar_t, 32768> crashDirectory{};
    LPTOP_LEVEL_EXCEPTION_FILTER previous{};
    bool installed{false};
};

CrashReporterState& state() noexcept {
    static CrashReporterState instance;
    return instance;
}

[[nodiscard]] bool copyPath(const std::filesystem::path& path,
                            std::array<wchar_t, 32768>& target) noexcept {
    const auto& native = path.native();
    if (native.empty() || native.size() >= target.size()) {
        return false;
    }
    std::wmemcpy(target.data(), native.data(), native.size());
    target[native.size()] = L'\0';
    return true;
}

[[nodiscard]] LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exceptionPointers) noexcept {
    const auto& current = state();
    if (current.crashDirectory[0] == L'\0') {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    SYSTEMTIME time{};
    ::GetLocalTime(&time);
    const DWORD processId = ::GetCurrentProcessId();
    const DWORD threadId = ::GetCurrentThreadId();

    std::array<wchar_t, 32768> dumpPath{};
    std::array<wchar_t, 32768> reportPath{};
    const int dumpLength = std::swprintf(
        dumpPath.data(), dumpPath.size(),
        L"%ls\\crash-%04u%02u%02u-%02u%02u%02u-p%lu-t%lu.dmp",
        current.crashDirectory.data(),
        static_cast<unsigned int>(time.wYear),
        static_cast<unsigned int>(time.wMonth),
        static_cast<unsigned int>(time.wDay),
        static_cast<unsigned int>(time.wHour),
        static_cast<unsigned int>(time.wMinute),
        static_cast<unsigned int>(time.wSecond),
        static_cast<unsigned long>(processId),
        static_cast<unsigned long>(threadId));
    const int reportLength = std::swprintf(
        reportPath.data(), reportPath.size(),
        L"%ls\\crash-%04u%02u%02u-%02u%02u%02u-p%lu-t%lu.txt",
        current.crashDirectory.data(),
        static_cast<unsigned int>(time.wYear),
        static_cast<unsigned int>(time.wMonth),
        static_cast<unsigned int>(time.wDay),
        static_cast<unsigned int>(time.wHour),
        static_cast<unsigned int>(time.wMinute),
        static_cast<unsigned int>(time.wSecond),
        static_cast<unsigned long>(processId),
        static_cast<unsigned long>(threadId));
    if (dumpLength <= 0 || reportLength <= 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const DWORD exceptionCode = exceptionPointers != nullptr &&
                                        exceptionPointers->ExceptionRecord != nullptr
                                    ? exceptionPointers->ExceptionRecord->ExceptionCode
                                    : 0U;
    const void* exceptionAddress = exceptionPointers != nullptr &&
                                           exceptionPointers->ExceptionRecord != nullptr
                                       ? exceptionPointers->ExceptionRecord->ExceptionAddress
                                       : nullptr;

    HANDLE dump = ::CreateFileW(dumpPath.data(),
                                GENERIC_WRITE,
                                FILE_SHARE_READ,
                                nullptr,
                                CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
        exceptionInfo.ThreadId = threadId;
        exceptionInfo.ExceptionPointers = exceptionPointers;
        exceptionInfo.ClientPointers = FALSE;

        const auto dumpType = static_cast<MINIDUMP_TYPE>(
            MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules |
            MiniDumpFilterMemory | MiniDumpWithoutOptionalData);
        static_cast<void>(::MiniDumpWriteDump(::GetCurrentProcess(),
                                              processId,
                                              dump,
                                              dumpType,
                                              exceptionPointers == nullptr ? nullptr : &exceptionInfo,
                                              nullptr,
                                              nullptr));
        static_cast<void>(::FlushFileBuffers(dump));
        ::CloseHandle(dump);
    }

    HANDLE report = ::CreateFileW(reportPath.data(),
                                  GENERIC_WRITE,
                                  FILE_SHARE_READ,
                                  nullptr,
                                  CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
    if (report != INVALID_HANDLE_VALUE) {
        std::array<char, 1024> text{};
        const int length = std::snprintf(
            text.data(), text.size(),
            "notepadFasaFiso crash report\r\n"
            "exception_code=0x%08lX\r\n"
            "exception_address=%p\r\n"
            "process_id=%lu\r\n"
            "thread_id=%lu\r\n"
            "note=The minidump is memory-filtered, but diagnostic stack/module data may still contain sensitive information. Review it before sharing.\r\n",
            static_cast<unsigned long>(exceptionCode),
            exceptionAddress,
            static_cast<unsigned long>(processId),
            static_cast<unsigned long>(threadId));
        if (length > 0) {
            const auto bytes = static_cast<DWORD>(
                length < static_cast<int>(text.size()) ? length : static_cast<int>(text.size() - 1U));
            DWORD ignored = 0U;
            static_cast<void>(::WriteFile(report, text.data(), bytes, &ignored, nullptr));
            static_cast<void>(::FlushFileBuffers(report));
        }
        ::CloseHandle(report);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

}

void installPlatformCrashHandler(const DiagnosticsPaths& paths) noexcept {
    auto& current = state();
    if (!copyPath(paths.crashDirectory, current.crashDirectory)) {
        current.crashDirectory[0] = L'\0';
        return;
    }

    current.previous = ::SetUnhandledExceptionFilter(&unhandledExceptionFilter);
    current.installed = true;
}

void uninstallPlatformCrashHandler() noexcept {
    auto& current = state();
    if (current.installed) {
        static_cast<void>(::SetUnhandledExceptionFilter(current.previous));
    }
    current = {};
}

}
