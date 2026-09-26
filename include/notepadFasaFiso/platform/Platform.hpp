#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace nff::platform {

struct CommandLineResult final {
    std::vector<std::string> arguments{};
    std::error_code error{};

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

struct FontEnumerationResult final {
    std::vector<std::string> families{};
    std::error_code error{};

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

[[nodiscard]] CommandLineResult commandLineArguments(int argc, char* const* argv);
[[nodiscard]] CommandLineResult nativeCommandLineArguments(int argc, char* const* argv);

[[nodiscard]] std::filesystem::path temporarySiblingPath(const std::filesystem::path& target);
[[nodiscard]] bool replaceFile(const std::filesystem::path& temporary,
                               const std::filesystem::path& target,
                               std::error_code& error) noexcept;
[[nodiscard]] std::error_code ensurePrivateDirectory(const std::filesystem::path& path) noexcept;
[[nodiscard]] std::error_code hardenPrivateFile(const std::filesystem::path& path) noexcept;
[[nodiscard]] std::uint64_t availablePhysicalMemoryBytes() noexcept;
[[nodiscard]] std::filesystem::path applicationDataDirectory();
[[nodiscard]] FontEnumerationResult systemFontFamilies();

#if defined(_WIN32)
void refreshFileAssociations() noexcept;
#endif

}
