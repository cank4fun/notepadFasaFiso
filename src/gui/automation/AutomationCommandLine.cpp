#include "notepadFasaFiso/gui/automation/AutomationCommandLine.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace nff::gui::automation {

std::wstring quoteWindowsArgument(const std::wstring_view argument) {
    const bool needsQuotes = argument.empty() ||
        std::any_of(argument.begin(), argument.end(), [](const wchar_t ch) {
            return ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\v' || ch == L'"';
        });
    if (!needsQuotes) {
        return std::wstring(argument);
    }

    std::wstring result;
    result.reserve(argument.size() + 2U);
    result.push_back(L'"');

    std::size_t backslashes = 0U;
    for (const wchar_t ch : argument) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(L'"');
            backslashes = 0U;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0U;
        result.push_back(ch);
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'"');
    return result;
}

std::wstring buildWindowsCommandLine(const std::span<const std::wstring> arguments) {
    std::wstring result;
    bool first = true;
    for (const auto& argument : arguments) {
        if (!first) {
            result.push_back(L' ');
        }
        first = false;
        result += quoteWindowsArgument(argument);
    }
    return result;
}

std::expected<std::optional<std::string>, std::string>
extractAutomationStateRootArgument(std::vector<std::string>& arguments) {
    constexpr std::string_view flag = "--nff-automation-state-root";
    std::optional<std::size_t> flagIndex;

    for (std::size_t index = 0U; index < arguments.size(); ++index) {
        if (arguments[index] != flag) {
            continue;
        }
        if (flagIndex.has_value()) {
            return std::unexpected("duplicate automation state root");
        }
        if (index + 1U >= arguments.size() || arguments[index + 1U].empty()) {
            return std::unexpected("automation state root requires a value");
        }
        flagIndex = index;
    }

    if (!flagIndex.has_value()) {
        return std::optional<std::string>{};
    }

    const auto index = *flagIndex;
    std::string root = arguments[index + 1U];
    arguments.erase(arguments.begin() + static_cast<std::ptrdiff_t>(index),
                    arguments.begin() + static_cast<std::ptrdiff_t>(index + 2U));
    return std::optional<std::string>{std::move(root)};
}

std::expected<std::vector<std::wstring>, std::string>
withAutomationStateRootArgument(const std::span<const std::wstring> arguments,
                                const std::wstring_view stateRoot) {
    if (stateRoot.empty()) {
        return std::unexpected("automation state root requires a value");
    }
    std::vector<std::wstring> result(arguments.begin(), arguments.end());
    result.emplace_back(L"--nff-automation-state-root");
    result.emplace_back(stateRoot);
    return result;
}

bool isSafeRelativeArtifactPath(const std::filesystem::path& path) noexcept {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }

    const auto generic = path.generic_string();
    if (generic.empty() || generic.front() == '/' || generic.front() == '\\') {
        return false;
    }
    if (generic.size() >= 2U &&
        ((generic[0] >= 'A' && generic[0] <= 'Z') || (generic[0] >= 'a' && generic[0] <= 'z')) &&
        generic[1] == ':') {
        return false;
    }
    if (generic.starts_with("//") || generic.starts_with("\\\\")) {
        return false;
    }

    for (const auto& component : path) {
        const auto value = component.generic_string();
        if (value == ".." || value.empty()) {
            return false;
        }
    }
    return true;
}

}
