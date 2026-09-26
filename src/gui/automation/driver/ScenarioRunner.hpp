#pragma once

#if defined(_WIN32)

#include "GuiDriverWindows.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace nff::gui::automation::driver {

struct ScenarioRunOptions final {
    std::filesystem::path scenarioPath;
    std::optional<std::filesystem::path> appPath;
    std::vector<std::wstring> appArguments;
    std::filesystem::path artifactRoot;
};

[[nodiscard]] std::expected<void, std::string>
runScenario(const ScenarioRunOptions& options);

[[nodiscard]] std::expected<void, windows::DriverError>
writeSnapshot(windows::TargetSession& session,
              const std::filesystem::path& artifactRoot,
              const std::filesystem::path& relativeDirectory);

[[nodiscard]] std::expected<void, windows::DriverError>
writeScreenshot(windows::TargetSession& session,
                const std::filesystem::path& outputPath);

}

#endif
