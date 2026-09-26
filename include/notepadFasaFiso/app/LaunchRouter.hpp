#pragma once

#include "notepadFasaFiso/app/LaunchRequest.hpp"
#include "notepadFasaFiso/core/FileSniffer.hpp"

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <system_error>
#include <vector>

namespace nff::app {

enum class RoutedInputKind : std::uint8_t {
    ExistingFile,
    NewFile,
    StandardInput
};

struct RoutedTarget {
    LaunchTarget requested{};
    RoutedInputKind inputKind{RoutedInputKind::ExistingFile};
    std::filesystem::path backingPath{};
    std::optional<core::DocumentProfile> profile{};
    core::OpenMode openMode{core::OpenMode::Editor};
    bool temporary{false};
    std::error_code error{};

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

struct LaunchRouteOptions {
    core::InspectOptions inspect{};
    std::size_t stdinChunkBytes{1024U * 1024U};
};

class LaunchPlan final {
public:
    LaunchPlan() = default;
    ~LaunchPlan();

    LaunchPlan(const LaunchPlan&) = delete;
    LaunchPlan& operator=(const LaunchPlan&) = delete;
    LaunchPlan(LaunchPlan&& other) noexcept;
    LaunchPlan& operator=(LaunchPlan&& other) noexcept;

    [[nodiscard]] const std::vector<RoutedTarget>& targets() const noexcept;
    [[nodiscard]] std::vector<RoutedTarget>& targets() noexcept;
    [[nodiscard]] bool hasErrors() const noexcept;

private:
    friend class LaunchRouter;

    void clearOwnedTemporaryFiles() noexcept;

    std::vector<RoutedTarget> targets_{};
    std::vector<std::filesystem::path> ownedTemporaryFiles_{};
};

class LaunchRouter final {
public:
    [[nodiscard]] static LaunchPlan route(const LaunchRequest& request,
                                          std::istream& standardInput,
                                          const LaunchRouteOptions& options = {});
};

}
