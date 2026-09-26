#include "notepadFasaFiso/platform/Platform.hpp"

#include "notepadFasaFiso/fonts/SfntFontReader.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace nff::platform {
namespace {

[[nodiscard]] std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const char ch) {
        auto byte = static_cast<unsigned char>(ch);
        if (byte >= 'A' && byte <= 'Z') {
            byte = static_cast<unsigned char>(byte - 'A' + 'a');
        }
        return static_cast<char>(byte);
    });
    return value;
}

[[nodiscard]] bool fontFile(const std::filesystem::path& path) {
    const auto extension = lowerAscii(path.extension().string());
    return extension == ".ttf" || extension == ".otf" || extension == ".ttc";
}

void appendUniqueRoot(std::vector<std::filesystem::path>& roots,
                      const std::filesystem::path& root) {
    if (root.empty()) {
        return;
    }
    const auto normalized = root.lexically_normal();
    if (std::find(roots.begin(), roots.end(), normalized) == roots.end()) {
        roots.push_back(normalized);
    }
}

void appendDataDirectories(std::vector<std::filesystem::path>& roots,
                           const std::string_view value) {
    std::size_t begin = 0U;
    while (begin <= value.size()) {
        const auto end = value.find(':', begin);
        const auto length = end == std::string_view::npos ? value.size() - begin : end - begin;
        if (length > 0U) {
            appendUniqueRoot(roots, std::filesystem::path(value.substr(begin, length)) / "fonts");
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
}

[[nodiscard]] std::vector<std::filesystem::path> fontRoots() {
    std::vector<std::filesystem::path> roots;
    roots.reserve(8U);
    appendUniqueRoot(roots, "/usr/share/fonts");
    appendUniqueRoot(roots, "/usr/local/share/fonts");

    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        appendUniqueRoot(roots, std::filesystem::path(home) / ".fonts");
        appendUniqueRoot(roots, std::filesystem::path(home) / ".local" / "share" / "fonts");
    }
    if (const char* dataHome = std::getenv("XDG_DATA_HOME");
        dataHome != nullptr && *dataHome != '\0') {
        appendUniqueRoot(roots, std::filesystem::path(dataHome) / "fonts");
    }
    if (const char* dataDirectories = std::getenv("XDG_DATA_DIRS");
        dataDirectories != nullptr && *dataDirectories != '\0') {
        appendDataDirectories(roots, dataDirectories);
    }
    return roots;
}

}

CommandLineResult commandLineArguments(const int argc, char* const* argv) {
    CommandLineResult result;
    if (argc <= 1 || argv == nullptr) {
        return result;
    }

    result.arguments.reserve(static_cast<std::size_t>(argc - 1));
    for (int index = 1; index < argc; ++index) {
        if (argv[index] == nullptr) {
            return {{}, std::make_error_code(std::errc::invalid_argument)};
        }
        result.arguments.emplace_back(argv[index]);
    }
    return result;
}

CommandLineResult nativeCommandLineArguments(const int argc, char* const* argv) {
    return commandLineArguments(argc, argv);
}

bool replaceFile(const std::filesystem::path& temporary,
                 const std::filesystem::path& target,
                 std::error_code& error) noexcept {
    if (std::rename(temporary.c_str(), target.c_str()) == 0) {
        error.clear();
        return true;
    }

    error = std::error_code(errno, std::generic_category());
    return false;
}

std::uint64_t availablePhysicalMemoryBytes() noexcept {
    const long pages = ::sysconf(_SC_AVPHYS_PAGES);
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (pages <= 0 || pageSize <= 0) {
        return 0U;
    }
    return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize);
}

std::filesystem::path applicationDataDirectory() {
    if (const char* stateHome = std::getenv("XDG_STATE_HOME"); stateHome != nullptr && *stateHome != '\0') {
        return std::filesystem::path(stateHome) / "notepadFasaFiso";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".local" / "state" / "notepadFasaFiso";
    }
    return {};
}

FontEnumerationResult systemFontFamilies() {
    FontEnumerationResult result;
    for (const auto& root : fontRoots()) {
        std::error_code error;
        if (!std::filesystem::is_directory(root, error) || error) {
            continue;
        }

        std::filesystem::recursive_directory_iterator iterator(
            root, std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::recursive_directory_iterator end;
        if (error) {
            continue;
        }

        while (iterator != end) {
            std::error_code entryError;
            if (iterator->is_regular_file(entryError) && !entryError && fontFile(iterator->path())) {
                auto inspected = fonts::SfntFontReader::inspect(iterator->path());
                if (inspected) {
                    result.families.insert(result.families.end(),
                                           std::make_move_iterator(inspected.families.begin()),
                                           std::make_move_iterator(inspected.families.end()));
                }
            }
            iterator.increment(entryError);
        }
    }
    return result;
}

}
