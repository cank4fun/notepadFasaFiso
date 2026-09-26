#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nff::gui::automation {

[[nodiscard]] std::wstring quoteWindowsArgument(std::wstring_view argument);
[[nodiscard]] std::wstring buildWindowsCommandLine(std::span<const std::wstring> arguments);
[[nodiscard]] bool isSafeRelativeArtifactPath(const std::filesystem::path& path) noexcept;
[[nodiscard]] std::expected<std::optional<std::string>, std::string>
extractAutomationStateRootArgument(std::vector<std::string>& arguments);
[[nodiscard]] std::expected<std::vector<std::wstring>, std::string>
withAutomationStateRootArgument(std::span<const std::wstring> arguments,
                                std::wstring_view stateRoot);

}
