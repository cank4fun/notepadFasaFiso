#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace nff::gui::automation {

enum class ScenarioCommandKind {
    Launch,
    Attach,
    Wait,
    Tree,
    Status,
    Click,
    Type,
    Shortcut,
    Drag,
    Maximize,
    Restore,
    Resize,
    Screenshot,
    Snapshot,
    Close,
};

struct ScenarioCommand final {
    ScenarioCommandKind kind{};
    std::size_t line{};
    std::vector<std::string> arguments;
    std::uint32_t durationMs{200U};
};

struct AutomationScenario final {
    std::vector<ScenarioCommand> commands;
};

enum class ScenarioParseErrorCode {
    UnterminatedQuote,
    InvalidEscape,
    UnknownCommand,
    InvalidArgumentCount,
    InvalidSemanticId,
    InvalidNumber,
    InvalidDimension,
    InvalidOption,
};

struct ScenarioParseError final {
    ScenarioParseErrorCode code{};
    std::size_t line{};
    std::string message;
};

[[nodiscard]] std::expected<AutomationScenario, ScenarioParseError>
parseScenario(std::string_view text);

}
