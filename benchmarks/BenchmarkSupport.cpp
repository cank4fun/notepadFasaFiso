#include "BenchmarkSupport.hpp"

#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace nff::bench {
namespace {

std::atomic<std::uint64_t> tempSequence{1U};

[[nodiscard]] std::filesystem::path makeTempRoot(const std::string_view prefix) {
    std::error_code error;
    auto base = std::filesystem::temp_directory_path(error);
    if (error) {
        base = std::filesystem::current_path(error);
    }
    if (error) {
        base = ".";
    }

    for (std::uint64_t attempt = 0U; attempt < 128U; ++attempt) {
        const auto sequence = tempSequence.fetch_add(1U, std::memory_order_relaxed);
        const auto candidate = base / (std::string(prefix) + "-" + std::to_string(sequence));
        error.clear();
        if (std::filesystem::create_directories(candidate, error)) {
            return candidate;
        }
        if (error && error != std::errc::file_exists) {
            break;
        }
    }
    return {};
}

[[nodiscard]] std::string escapeTsv(std::string value) {
    for (char& byte : value) {
        if (byte == '\t' || byte == '\r' || byte == '\n') {
            byte = ' ';
        }
    }
    return value;
}

}

Stopwatch::Stopwatch() noexcept : started_(Clock::now()) {}

std::uint64_t Stopwatch::elapsedMicroseconds() const noexcept {
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started_);
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed.count()));
}

double Stopwatch::elapsedMilliseconds() const noexcept {
    return static_cast<double>(elapsedMicroseconds()) / 1000.0;
}

TempFixture::TempFixture(const std::string_view prefix) : root_(makeTempRoot(prefix)) {
    if (root_.empty()) {
        throw std::runtime_error("unable to create temporary benchmark directory");
    }
}

TempFixture::~TempFixture() {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
}

const std::filesystem::path& TempFixture::root() const noexcept {
    return root_;
}

std::filesystem::path TempFixture::path(const std::string_view filename) const {
    return root_ / std::string(filename);
}

std::error_code writeBytes(const std::filesystem::path& path,
                           const std::span<const std::byte> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return std::make_error_code(std::errc::io_error);
    }
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    if (!output) {
        return std::make_error_code(std::errc::io_error);
    }
    return {};
}

std::error_code writeRepeatedText(const std::filesystem::path& path,
                                  const std::string_view line,
                                  const std::uint64_t targetBytes,
                                  const std::string_view tail) {
    if (line.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return std::make_error_code(std::errc::io_error);
    }

    std::string block;
    block.reserve(1024U * 1024U);
    while (block.size() + line.size() <= 1024U * 1024U) {
        block.append(line);
    }
    if (block.empty()) {
        block.assign(line);
    }

    const std::uint64_t reservedTail = std::min<std::uint64_t>(
        targetBytes, static_cast<std::uint64_t>(tail.size()));
    const std::uint64_t bodyTarget = targetBytes - reservedTail;
    std::uint64_t written = 0U;
    while (written < bodyTarget) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(block.size()), bodyTarget - written));
        output.write(block.data(), static_cast<std::streamsize>(count));
        if (!output) {
            return std::make_error_code(std::errc::io_error);
        }
        written += static_cast<std::uint64_t>(count);
    }
    if (reservedTail != 0U) {
        const auto tailOffset = tail.size() - static_cast<std::size_t>(reservedTail);
        output.write(tail.data() + tailOffset, static_cast<std::streamsize>(reservedTail));
    }
    if (!output) {
        return std::make_error_code(std::errc::io_error);
    }
    return {};
}

std::error_code createSparseFile(const std::filesystem::path& path,
                                 const std::string_view prefix,
                                 const std::uint64_t totalBytes) {
    if (totalBytes < static_cast<std::uint64_t>(prefix.size())) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            return std::make_error_code(std::errc::io_error);
        }
        output.write(prefix.data(), static_cast<std::streamsize>(prefix.size()));
        if (!output) {
            return std::make_error_code(std::errc::io_error);
        }
    }

#ifdef _WIN32
    HANDLE handle = ::CreateFileW(path.c_str(),
                                  GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return {static_cast<int>(::GetLastError()), std::system_category()};
    }
    DWORD returned = 0U;
    if (::DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0U, nullptr, 0U, &returned, nullptr) == FALSE) {
        const auto error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
        ::CloseHandle(handle);
        return error;
    }
    LARGE_INTEGER offset{};
    offset.QuadPart = static_cast<LONGLONG>(totalBytes);
    if (::SetFilePointerEx(handle, offset, nullptr, FILE_BEGIN) == FALSE || ::SetEndOfFile(handle) == FALSE) {
        const auto error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
        ::CloseHandle(handle);
        return error;
    }
    ::CloseHandle(handle);
    return {};
#else
    const int descriptor = ::open(path.c_str(), O_WRONLY);
    if (descriptor < 0) {
        return {errno, std::generic_category()};
    }
    if (totalBytes > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        ::close(descriptor);
        return std::make_error_code(std::errc::file_too_large);
    }
    if (::ftruncate(descriptor, static_cast<off_t>(totalBytes)) != 0) {
        const auto error = std::error_code(errno, std::generic_category());
        ::close(descriptor);
        return error;
    }
    ::close(descriptor);
    return {};
#endif
}

std::string formatUnsigned(const std::uint64_t value) {
    return std::to_string(value);
}

std::string formatSigned(const std::int64_t value) {
    return std::to_string(value);
}

std::string formatDouble(const double value, const int precision) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(std::max(0, precision)) << value;
    return stream.str();
}

Measurement measure(std::string name, const std::uint64_t value) {
    return {std::move(name), formatUnsigned(value)};
}

Measurement measure(std::string name, const std::int64_t value) {
    return {std::move(name), formatSigned(value)};
}

Measurement measure(std::string name, const double value, const int precision) {
    return {std::move(name), formatDouble(value, precision)};
}

Measurement measure(std::string name, std::string value) {
    return {std::move(name), std::move(value)};
}

BenchmarkReporter::BenchmarkReporter(std::ostream& output, const Format format) noexcept
    : output_(&output), format_(format) {}

void BenchmarkReporter::report(const BenchmarkResult& result) const {
    if (output_ == nullptr) {
        return;
    }

    if (format_ == Format::Tsv) {
        *output_ << escapeTsv(result.name) << '\t' << (result.passed ? "PASS" : "FAIL");
        for (const auto& measurement : result.measurements) {
            *output_ << '\t' << escapeTsv(measurement.name) << '=' << escapeTsv(measurement.value);
        }
        if (!result.detail.empty()) {
            *output_ << "\tdetail=" << escapeTsv(result.detail);
        }
        *output_ << '\n';
        return;
    }

    *output_ << result.name;
    for (const auto& measurement : result.measurements) {
        *output_ << ' ' << measurement.name << '=' << measurement.value;
    }
    *output_ << " status=" << (result.passed ? "PASS" : "FAIL");
    if (!result.detail.empty()) {
        *output_ << " detail=\"" << result.detail << '"';
    }
    *output_ << '\n';
}

int runBenchmarkProgram(const int argc,
                        char** argv,
                        const std::vector<BenchmarkCase>& cases,
                        std::ostream& output,
                        std::ostream& error) {
    BenchmarkReporter::Format format = BenchmarkReporter::Format::Human;
    bool listOnly = false;
    std::optional<std::string> selected;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--tsv") {
            format = BenchmarkReporter::Format::Tsv;
        } else if (argument == "--list") {
            listOnly = true;
        } else if (argument == "--case") {
            if (index + 1 >= argc) {
                error << "--case requires a case name\n";
                return 2;
            }
            selected = std::string(argv[++index]);
        } else if (argument == "--help" || argument == "-h") {
            output << "Usage: nff_benchmarks [--list] [--case NAME] [--tsv]\n";
            return 0;
        } else {
            error << "unknown argument: " << argument << '\n';
            return 2;
        }
    }

    if (listOnly) {
        for (const auto& benchmark : cases) {
            output << benchmark.name << '\n';
        }
        return 0;
    }

    BenchmarkReporter reporter(output, format);
    bool failed = false;
    bool found = !selected.has_value();
    for (const auto& benchmark : cases) {
        if (selected && benchmark.name != *selected) {
            continue;
        }
        found = true;
        try {
            auto result = benchmark.run();
            if (result.name.empty()) {
                result.name = benchmark.name;
            }
            failed = failed || !result.passed;
            reporter.report(result);
        } catch (const std::exception& exception) {
            failed = true;
            reporter.report(BenchmarkResult{benchmark.name, {}, false, exception.what()});
        } catch (...) {
            failed = true;
            reporter.report(BenchmarkResult{benchmark.name, {}, false, "unknown exception"});
        }
    }

    if (!found) {
        error << "unknown benchmark case: " << *selected << '\n';
        return 2;
    }
    return failed ? 1 : 0;
}

}
