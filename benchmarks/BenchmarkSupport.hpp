#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nff::bench {

struct Measurement final {
    std::string name;
    std::string value;
};

struct BenchmarkResult final {
    std::string name;
    std::vector<Measurement> measurements;
    bool passed{true};
    std::string detail;
};

using BenchmarkFunction = std::function<BenchmarkResult()>;

struct BenchmarkCase final {
    std::string name;
    BenchmarkFunction run;
};

class Stopwatch final {
public:
    using Clock = std::chrono::steady_clock;

    Stopwatch() noexcept;
    [[nodiscard]] std::uint64_t elapsedMicroseconds() const noexcept;
    [[nodiscard]] double elapsedMilliseconds() const noexcept;

private:
    Clock::time_point started_;
};

class TempFixture final {
public:
    explicit TempFixture(std::string_view prefix);
    TempFixture(const TempFixture&) = delete;
    TempFixture& operator=(const TempFixture&) = delete;
    TempFixture(TempFixture&&) = delete;
    TempFixture& operator=(TempFixture&&) = delete;
    ~TempFixture();

    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] std::filesystem::path path(std::string_view filename) const;

private:
    std::filesystem::path root_;
};

[[nodiscard]] std::error_code writeBytes(const std::filesystem::path& path,
                                         std::span<const std::byte> bytes);
[[nodiscard]] std::error_code writeRepeatedText(const std::filesystem::path& path,
                                                std::string_view line,
                                                std::uint64_t targetBytes,
                                                std::string_view tail = {});
[[nodiscard]] std::error_code createSparseFile(const std::filesystem::path& path,
                                               std::string_view prefix,
                                               std::uint64_t totalBytes);

[[nodiscard]] std::string formatUnsigned(std::uint64_t value);
[[nodiscard]] std::string formatSigned(std::int64_t value);
[[nodiscard]] std::string formatDouble(double value, int precision = 3);
[[nodiscard]] Measurement measure(std::string name, std::uint64_t value);
[[nodiscard]] Measurement measure(std::string name, std::int64_t value);
[[nodiscard]] Measurement measure(std::string name, double value, int precision = 3);
[[nodiscard]] Measurement measure(std::string name, std::string value);

class BenchmarkReporter final {
public:
    enum class Format {
        Human,
        Tsv,
    };

    BenchmarkReporter(std::ostream& output, Format format) noexcept;
    void report(const BenchmarkResult& result) const;

private:
    std::ostream* output_{};
    Format format_{Format::Human};
};

[[nodiscard]] int runBenchmarkProgram(int argc,
                                      char** argv,
                                      const std::vector<BenchmarkCase>& cases,
                                      std::ostream& output,
                                      std::ostream& error);

}
