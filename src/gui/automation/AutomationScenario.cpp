#include "notepadFasaFiso/gui/automation/AutomationScenario.hpp"

#include "notepadFasaFiso/gui/automation/AutomationProtocol.hpp"

#include <charconv>
#include <cctype>
#include <limits>
#include <optional>

namespace nff::gui::automation {
namespace {

std::expected<std::vector<std::string>, ScenarioParseError>
tokenizeLine(const std::string_view line, const std::size_t lineNumber) {
    std::vector<std::string> tokens;
    std::size_t index = 0U;
    while (index < line.size()) {
        while (index < line.size() && (line[index] == ' ' || line[index] == '\t')) {
            ++index;
        }
        if (index >= line.size() || line[index] == '#') {
            break;
        }

        std::string token;
        if (line[index] == '"') {
            ++index;
            bool closed = false;
            while (index < line.size()) {
                const char ch = line[index++];
                if (ch == '"') {
                    closed = true;
                    break;
                }
                if (ch != '\\') {
                    token.push_back(ch);
                    continue;
                }
                if (index >= line.size()) {
                    return std::unexpected(ScenarioParseError{
                        ScenarioParseErrorCode::InvalidEscape, lineNumber,
                        "trailing escape in quoted argument"});
                }
                const char escaped = line[index++];
                switch (escaped) {
                case '\\': token.push_back('\\'); break;
                case '"': token.push_back('"'); break;
                case 't': token.push_back('\t'); break;
                case 'n': token.push_back('\n'); break;
                case 'r': token.push_back('\r'); break;
                default:
                    return std::unexpected(ScenarioParseError{
                        ScenarioParseErrorCode::InvalidEscape, lineNumber,
                        "unsupported escape in quoted argument"});
                }
            }
            if (!closed) {
                return std::unexpected(ScenarioParseError{
                    ScenarioParseErrorCode::UnterminatedQuote, lineNumber,
                    "unterminated quoted argument"});
            }
            if (index < line.size() && line[index] != ' ' && line[index] != '\t' &&
                line[index] != '#') {
                return std::unexpected(ScenarioParseError{
                    ScenarioParseErrorCode::InvalidArgumentCount, lineNumber,
                    "quoted argument must end before the next token"});
            }
        } else {
            while (index < line.size() && line[index] != ' ' && line[index] != '\t' &&
                   line[index] != '#') {
                token.push_back(line[index++]);
            }
        }
        tokens.push_back(std::move(token));
        if (index < line.size() && line[index] == '#') {
            break;
        }
    }
    return tokens;
}

std::optional<long long> parseNumber(const std::string_view value) {
    long long parsed{};
    const auto* first = value.data();
    const auto* last = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(first, last, parsed);
    if (error != std::errc{} || ptr != last) {
        return std::nullopt;
    }
    return parsed;
}

std::expected<ScenarioCommand, ScenarioParseError>
parseCommand(const std::vector<std::string>& tokens, const std::size_t lineNumber) {
    auto error = [&](const ScenarioParseErrorCode code, const std::string_view message) {
        return std::expected<ScenarioCommand, ScenarioParseError>{std::unexpected(
            ScenarioParseError{code, lineNumber, std::string(message)})};
    };
    if (tokens.empty()) {
        return error(ScenarioParseErrorCode::UnknownCommand, "empty command");
    }

    const auto& verb = tokens[0];
    ScenarioCommand command{};
    command.line = lineNumber;

    auto requireCount = [&](const std::size_t count) -> bool {
        return tokens.size() == count + 1U;
    };
    auto requireSemantic = [&](const std::size_t index) -> bool {
        return index < tokens.size() && isAddressableAutomationId(tokens[index]);
    };

    if (verb == "click") {
        if (!requireCount(1U)) return error(ScenarioParseErrorCode::InvalidArgumentCount, "click expects one semantic id");
        if (!requireSemantic(1U)) return error(ScenarioParseErrorCode::InvalidSemanticId, "click semantic id is invalid");
        command.kind = ScenarioCommandKind::Click;
        command.arguments = {tokens[1]};
        return command;
    }
    if (verb == "type") {
        if (!requireCount(1U)) return error(ScenarioParseErrorCode::InvalidArgumentCount, "type expects one text argument");
        command.kind = ScenarioCommandKind::Type;
        command.arguments = {tokens[1]};
        return command;
    }
    if (verb == "shortcut") {
        if (!requireCount(1U)) return error(ScenarioParseErrorCode::InvalidArgumentCount, "shortcut expects one key chord");
        command.kind = ScenarioCommandKind::Shortcut;
        command.arguments = {tokens[1]};
        return command;
    }
    if (verb == "wait") {
        if (!requireCount(2U)) return error(ScenarioParseErrorCode::InvalidArgumentCount, "wait expects semantic id and state");
        if (!requireSemantic(1U)) return error(ScenarioParseErrorCode::InvalidSemanticId, "wait semantic id is invalid");
        if (tokens[2] != "visible" && tokens[2] != "enabled" && tokens[2] != "checked" &&
            tokens[2] != "hidden") {
            return error(ScenarioParseErrorCode::InvalidOption,
                         "wait state must be visible, enabled, checked, or hidden");
        }
        command.kind = ScenarioCommandKind::Wait;
        command.arguments = {tokens[1], tokens[2]};
        return command;
    }
    if (verb == "drag") {
        if (tokens.size() != 3U && tokens.size() != 5U) {
            return error(ScenarioParseErrorCode::InvalidArgumentCount, "drag expects source and target semantic ids with optional duration");
        }
        if (!requireSemantic(1U) || !requireSemantic(2U)) {
            return error(ScenarioParseErrorCode::InvalidSemanticId, "drag semantic id is invalid");
        }
        command.kind = ScenarioCommandKind::Drag;
        command.arguments = {tokens[1], tokens[2]};
        if (tokens.size() == 5U) {
            if (tokens[3] != "--duration-ms") {
                return error(ScenarioParseErrorCode::InvalidOption, "drag only supports --duration-ms");
            }
            const auto duration = parseNumber(tokens[4]);
            if (!duration || *duration <= 0 ||
                *duration > static_cast<long long>(std::numeric_limits<std::uint32_t>::max())) {
                return error(ScenarioParseErrorCode::InvalidNumber, "drag duration must be a positive uint32 value");
            }
            command.durationMs = static_cast<std::uint32_t>(*duration);
        }
        return command;
    }
    if (verb == "resize") {
        if (!requireCount(4U)) return error(ScenarioParseErrorCode::InvalidArgumentCount, "resize expects x y width height");
        for (std::size_t index = 1U; index <= 4U; ++index) {
            const auto value = parseNumber(tokens[index]);
            if (!value || *value < static_cast<long long>(std::numeric_limits<int>::min()) ||
                *value > static_cast<long long>(std::numeric_limits<int>::max())) {
                return error(ScenarioParseErrorCode::InvalidNumber, "resize value is not a valid int");
            }
            if (index >= 3U && *value <= 0) {
                return error(ScenarioParseErrorCode::InvalidDimension, "resize width and height must be positive");
            }
        }
        command.kind = ScenarioCommandKind::Resize;
        command.arguments.assign(tokens.begin() + 1, tokens.end());
        return command;
    }
    if (verb == "attach") {
        if (!requireCount(1U)) return error(ScenarioParseErrorCode::InvalidArgumentCount, "attach expects one pid");
        const auto pid = parseNumber(tokens[1]);
        if (!pid || *pid <= 0 || *pid > static_cast<long long>(std::numeric_limits<std::uint32_t>::max())) {
            return error(ScenarioParseErrorCode::InvalidNumber, "attach pid must be a positive uint32 value");
        }
        command.kind = ScenarioCommandKind::Attach;
        command.arguments = {tokens[1]};
        return command;
    }
    if (verb == "launch") {
        if (tokens.size() < 3U || tokens[1] != "--app") {
            return error(ScenarioParseErrorCode::InvalidArgumentCount, "launch requires --app <path>");
        }
        command.kind = ScenarioCommandKind::Launch;
        command.arguments.assign(tokens.begin() + 2, tokens.end());
        return command;
    }
    if (verb == "screenshot" || verb == "snapshot") {
        if (!requireCount(1U)) return error(ScenarioParseErrorCode::InvalidArgumentCount, "capture command expects one output path");
        command.kind = verb == "screenshot" ? ScenarioCommandKind::Screenshot : ScenarioCommandKind::Snapshot;
        command.arguments = {tokens[1]};
        return command;
    }
    if (verb == "tree" || verb == "status" || verb == "maximize" || verb == "restore" ||
        verb == "close") {
        if (!requireCount(0U)) return error(ScenarioParseErrorCode::InvalidArgumentCount, "command does not accept arguments");
        if (verb == "tree") command.kind = ScenarioCommandKind::Tree;
        else if (verb == "status") command.kind = ScenarioCommandKind::Status;
        else if (verb == "maximize") command.kind = ScenarioCommandKind::Maximize;
        else if (verb == "restore") command.kind = ScenarioCommandKind::Restore;
        else command.kind = ScenarioCommandKind::Close;
        return command;
    }

    return error(ScenarioParseErrorCode::UnknownCommand, "unknown scenario command");
}

}

std::expected<AutomationScenario, ScenarioParseError> parseScenario(const std::string_view text) {
    AutomationScenario scenario;
    std::size_t lineNumber = 1U;
    std::size_t begin = 0U;
    while (begin <= text.size()) {
        const auto end = text.find('\n', begin);
        auto line = end == std::string_view::npos ? text.substr(begin) : text.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1U);
        }
        const auto tokens = tokenizeLine(line, lineNumber);
        if (!tokens) {
            return std::unexpected(tokens.error());
        }
        if (!tokens->empty()) {
            const auto command = parseCommand(*tokens, lineNumber);
            if (!command) {
                return std::unexpected(command.error());
            }
            scenario.commands.push_back(*command);
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
        ++lineNumber;
    }
    return scenario;
}

}
