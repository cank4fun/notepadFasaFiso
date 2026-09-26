#include "notepadFasaFiso/diagnostics/LocalDiagnostics.hpp"

#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#ifdef __linux__
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::filesystem::path tempRoot() {
    return std::filesystem::temp_directory_path() / "nff-diagnostics-tests";
}

void testInitializationCreatesNoRoutineLog() {
    const auto root = tempRoot();
    std::error_code error;
    std::filesystem::remove_all(root, error);

    const auto initialized = nff::diagnostics::initialize(root);
    expect(static_cast<bool>(initialized), "diagnostics initialize");
    expect(initialized.paths.directory == root / "diagnostics",
           "diagnostics directory uses application data root");
    expect(initialized.paths.exceptionFile == root / "diagnostics" / "exception-last.txt",
           "bounded exception file uses application data root");
    expect(initialized.paths.crashDirectory == root / "diagnostics" / "crashes",
           "crash directory uses application data root");
    expect(!std::filesystem::exists(root / "diagnostics" / "notepadFasaFiso.log"),
           "normal initialization creates no routine log");
    expect(!std::filesystem::exists(root / "diagnostics" / "notepadFasaFiso.log.1"),
           "normal initialization creates no rotated routine log");

    nff::diagnostics::shutdown();
    std::filesystem::remove_all(root, error);
}

void testExceptionReportIsLazySanitizedAndBounded() {
    const auto root = tempRoot();
    std::error_code error;
    std::filesystem::remove_all(root, error);

    const auto initialized = nff::diagnostics::initialize(root);
    expect(static_cast<bool>(initialized), "diagnostics initialize for exception report");
    expect(!std::filesystem::exists(initialized.paths.exceptionFile),
           "exception report is lazy");

    nff::diagnostics::writeExceptionReport("wx.exception\ncategory",
                                           "first\tmessage\nnext");
    expect(std::filesystem::is_regular_file(initialized.paths.exceptionFile),
           "exception creates local report");

    nff::diagnostics::writeExceptionReport("wx.exception", "second message");
    std::ifstream stream(initialized.paths.exceptionFile, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(stream)),
                               std::istreambuf_iterator<char>());
    expect(contents.find("notepadFasaFiso exception report") != std::string::npos,
           "report identifies application");
    expect(contents.find("category=wx.exception") != std::string::npos,
           "report records category");
    expect(contents.find("message=second message") != std::string::npos,
           "latest report replaces previous report");
    expect(contents.find("first") == std::string::npos,
           "report is bounded instead of append-only");
    expect(contents.find('\t') == std::string::npos &&
               contents.find("\nnext") == std::string::npos,
           "report payload is one-line sanitized");
    stream.close();

    const std::string oversizedMessage(16U * 1024U, 'x');
    nff::diagnostics::writeExceptionReport("wx.exception", oversizedMessage);
    const auto boundedSize = std::filesystem::file_size(initialized.paths.exceptionFile, error);
    expect(!error && boundedSize <= 4608U,
           "exception report has a hard size bound");

#ifdef __linux__
    struct stat directoryStatus {};
    struct stat crashDirectoryStatus {};
    struct stat exceptionStatus {};
    expect(::stat(initialized.paths.directory.c_str(), &directoryStatus) == 0 &&
               (directoryStatus.st_mode & 0777) == 0700,
           "Linux diagnostics directory is private");
    expect(::stat(initialized.paths.crashDirectory.c_str(), &crashDirectoryStatus) == 0 &&
               (crashDirectoryStatus.st_mode & 0777) == 0700,
           "Linux crash directory is private");
    expect(::stat(initialized.paths.exceptionFile.c_str(), &exceptionStatus) == 0 &&
               (exceptionStatus.st_mode & 0777) == 0600,
           "Linux exception report is private");
#endif

    nff::diagnostics::shutdown();
    std::filesystem::remove_all(root, error);
}

void testEmptyRootFailsWithoutWritingCurrentDirectory() {
    const auto initialized = nff::diagnostics::initialize({});
    expect(!initialized, "empty diagnostics root is rejected");
    expect(static_cast<bool>(initialized.error), "empty diagnostics root returns error");
    nff::diagnostics::shutdown();
}

#ifdef __linux__
void testLinuxCrashHandlerWritesLocalReport() {
    const auto root = std::filesystem::temp_directory_path() / "nff-linux-crash-report-test";
    std::error_code error;
    std::filesystem::remove_all(root, error);

    const auto child = ::fork();
    expect(child >= 0, "fork Linux crash reporter child");
    if (child < 0) {
        return;
    }
    if (child == 0) {
        const auto initialized = nff::diagnostics::initialize(root);
        if (!initialized) {
            ::_exit(90);
        }
        nff::diagnostics::installPlatformCrashHandler(initialized.paths);
        ::raise(SIGABRT);
        ::_exit(91);
    }

    int status = 0;
    expect(::waitpid(child, &status, 0) == child, "wait for Linux crash reporter child");

    const auto crashDirectory = root / "diagnostics" / "crashes";
    std::vector<std::filesystem::path> reports;
    for (std::filesystem::directory_iterator it(crashDirectory, error), end;
         !error && it != end; it.increment(error)) {
        if (it->is_regular_file()) {
            reports.push_back(it->path());
        }
    }
    expect(!error, "enumerate Linux crash reports");
    expect(reports.size() == 1U, "fatal Linux signal writes exactly one local crash report");
    if (reports.size() == 1U) {
        std::ifstream stream(reports.front(), std::ios::binary);
        const std::string contents((std::istreambuf_iterator<char>(stream)),
                                   std::istreambuf_iterator<char>());
        expect(contents.find("notepadFasaFiso crash report") != std::string::npos,
               "Linux crash report identifies the application");
        expect(contents.find("signal=") != std::string::npos,
               "Linux crash report records signal information");
        expect(contents.find("pid=") != std::string::npos,
               "Linux crash report records process id");
    }

    std::filesystem::remove_all(root, error);
}
#endif

}

int main() {
    testInitializationCreatesNoRoutineLog();
    testExceptionReportIsLazySanitizedAndBounded();
    testEmptyRootFailsWithoutWritingCurrentDirectory();
#ifdef __linux__
    testLinuxCrashHandlerWritesLocalReport();
#endif
    return failures == 0 ? 0 : 1;
}
