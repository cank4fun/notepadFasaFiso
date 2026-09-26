#include "notepadFasaFiso/app/LaunchRouter.hpp"

#include "notepadFasaFiso/platform/Platform.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <ios>
#include <istream>
#include <system_error>
#include <utility>
#include <vector>

namespace nff::app {
namespace {

struct SpoolResult {
    std::filesystem::path path{};
    std::error_code error{};
};

[[nodiscard]] SpoolResult spoolStandardInput(std::istream& input, const std::size_t chunkBytes) {
    if (chunkBytes == 0U) {
        return {{}, std::make_error_code(std::errc::invalid_argument)};
    }

    std::error_code error;
    const auto temporaryRoot = std::filesystem::temp_directory_path(error);
    if (error) {
        return {{}, error};
    }

    const auto seed = temporaryRoot / "notepadFasaFiso-stdin.txt";
    const auto path = platform::temporarySiblingPath(seed);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return {{}, std::make_error_code(std::errc::io_error)};
    }

    std::vector<char> buffer(chunkBytes);
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            output.write(buffer.data(), count);
            if (!output) {
                output.close();
                std::filesystem::remove(path, error);
                return {{}, std::make_error_code(std::errc::io_error)};
            }
        }
    }

    if (!input.eof()) {
        output.close();
        std::filesystem::remove(path, error);
        return {{}, std::make_error_code(std::errc::io_error)};
    }

    output.flush();
    if (!output) {
        output.close();
        std::filesystem::remove(path, error);
        return {{}, std::make_error_code(std::errc::io_error)};
    }
    output.close();
    return {path, {}};
}

[[nodiscard]] core::OpenMode applyModePreference(const core::OpenMode inspected,
                                                 const OpenModePreference preference) noexcept {
    if (inspected == core::OpenMode::BinaryPreview) {
        return core::OpenMode::BinaryPreview;
    }

    switch (preference) {
    case OpenModePreference::Automatic:
        return inspected;
    case OpenModePreference::Editor:
        return core::OpenMode::Editor;
    case OpenModePreference::Viewer:
        return core::OpenMode::Viewer;
    }
    return inspected;
}

[[nodiscard]] RoutedTarget inspectExistingTarget(const LaunchTarget& requested,
                                                 const std::filesystem::path& path,
                                                 const RoutedInputKind kind,
                                                 const bool temporary,
                                                 const LaunchRouteOptions& options) {
    RoutedTarget routed;
    routed.requested = requested;
    routed.inputKind = kind;
    routed.backingPath = path;
    routed.temporary = temporary;

    const auto inspected = core::FileSniffer::inspect(path, options.inspect);
    if (!inspected) {
        routed.error = inspected.error;
        return routed;
    }

    routed.profile = inspected.profile;
    routed.openMode = applyModePreference(inspected.profile.recommendedMode,
                                          requested.modePreference);
    return routed;
}

}

LaunchPlan::~LaunchPlan() {
    clearOwnedTemporaryFiles();
}

LaunchPlan::LaunchPlan(LaunchPlan&& other) noexcept
    : targets_(std::move(other.targets_)),
      ownedTemporaryFiles_(std::move(other.ownedTemporaryFiles_)) {
    other.ownedTemporaryFiles_.clear();
}

LaunchPlan& LaunchPlan::operator=(LaunchPlan&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    clearOwnedTemporaryFiles();
    targets_ = std::move(other.targets_);
    ownedTemporaryFiles_ = std::move(other.ownedTemporaryFiles_);
    other.ownedTemporaryFiles_.clear();
    return *this;
}

const std::vector<RoutedTarget>& LaunchPlan::targets() const noexcept {
    return targets_;
}

std::vector<RoutedTarget>& LaunchPlan::targets() noexcept {
    return targets_;
}

bool LaunchPlan::hasErrors() const noexcept {
    return std::any_of(targets_.begin(), targets_.end(), [](const RoutedTarget& target) {
        return static_cast<bool>(target.error);
    });
}

void LaunchPlan::clearOwnedTemporaryFiles() noexcept {
    for (const auto& path : ownedTemporaryFiles_) {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
    ownedTemporaryFiles_.clear();
}

LaunchPlan LaunchRouter::route(const LaunchRequest& request,
                               std::istream& standardInput,
                               const LaunchRouteOptions& options) {
    LaunchPlan plan;
    plan.targets_.reserve(request.targets.size());

    for (const auto& requested : request.targets) {
        if (requested.inputKind == LaunchInputKind::StandardInput) {
            const auto spooled = spoolStandardInput(standardInput, options.stdinChunkBytes);
            if (spooled.error) {
                RoutedTarget routed;
                routed.requested = requested;
                routed.inputKind = RoutedInputKind::StandardInput;
                routed.temporary = true;
                routed.error = spooled.error;
                plan.targets_.push_back(std::move(routed));
                continue;
            }

            plan.ownedTemporaryFiles_.push_back(spooled.path);
            plan.targets_.push_back(inspectExistingTarget(requested,
                                                          spooled.path,
                                                          RoutedInputKind::StandardInput,
                                                          true,
                                                          options));
            continue;
        }

        std::error_code error;
        const auto status = std::filesystem::status(requested.path, error);
        if (!error && std::filesystem::exists(status)) {
            if (!std::filesystem::is_regular_file(status)) {
                RoutedTarget routed;
                routed.requested = requested;
                routed.inputKind = RoutedInputKind::ExistingFile;
                routed.backingPath = requested.path;
                routed.error = std::make_error_code(std::errc::is_a_directory);
                plan.targets_.push_back(std::move(routed));
                continue;
            }

            plan.targets_.push_back(inspectExistingTarget(requested,
                                                          requested.path,
                                                          RoutedInputKind::ExistingFile,
                                                          false,
                                                          options));
            continue;
        }

        if (error && error != std::errc::no_such_file_or_directory) {
            RoutedTarget routed;
            routed.requested = requested;
            routed.inputKind = RoutedInputKind::ExistingFile;
            routed.backingPath = requested.path;
            routed.error = error;
            plan.targets_.push_back(std::move(routed));
            continue;
        }

        RoutedTarget routed;
        routed.requested = requested;
        routed.inputKind = RoutedInputKind::NewFile;
        routed.backingPath = requested.path;
        routed.openMode = core::OpenMode::Editor;
        plan.targets_.push_back(std::move(routed));
    }

    return plan;
}

}
