#pragma once

#include "notepadFasaFiso/core/FileSniffer.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nff::app {

enum class LaunchInputKind : std::uint8_t {
    File,
    StandardInput
};

enum class OpenModePreference : std::uint8_t {
    Automatic,
    Editor,
    Viewer
};

struct TextPosition {
    std::uint64_t line{1};
    std::uint64_t column{1};
    bool hasLine{false};
    bool hasColumn{false};
};

struct LaunchTarget {
    LaunchInputKind inputKind{LaunchInputKind::File};
    std::filesystem::path path{};
    TextPosition position{};
    OpenModePreference modePreference{OpenModePreference::Automatic};
};

struct LaunchRequest {
    std::vector<LaunchTarget> targets{};
    bool showHelp{false};
};

struct LaunchParseResult {
    LaunchRequest request{};
    std::string error{};

    [[nodiscard]] explicit operator bool() const noexcept { return error.empty(); }
};

class LaunchRequestParser final {
public:
    [[nodiscard]] static LaunchParseResult parse(std::span<const std::string_view> arguments);
    [[nodiscard]] static std::string_view usage() noexcept;
};

}
