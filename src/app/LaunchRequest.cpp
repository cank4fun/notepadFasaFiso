#include "notepadFasaFiso/app/LaunchRequest.hpp"

#include <charconv>
#include <cstddef>
#include <optional>
#include <system_error>

namespace nff::app {
namespace {

[[nodiscard]] std::filesystem::path pathFromUtf8(const std::string_view text) {
    const auto* begin = reinterpret_cast<const char8_t*>(text.data());
    return std::filesystem::path(std::u8string(begin, begin + text.size()));
}

[[nodiscard]] std::optional<std::uint64_t> parsePositiveInteger(const std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }

    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0) {
        return std::nullopt;
    }
    return value;
}

struct PathWithPosition {
    std::string_view path{};
    TextPosition position{};
};

[[nodiscard]] PathWithPosition parsePathPosition(const std::string_view argument) {
    PathWithPosition result{argument, {}};
    if (argument.empty()) {
        return result;
    }

    std::error_code error;
    if (std::filesystem::exists(pathFromUtf8(argument), error) && !error) {
        return result;
    }

    const auto lastColon = argument.rfind(':');
    if (lastColon == std::string_view::npos || lastColon + 1U >= argument.size()) {
        return result;
    }

    const auto lastNumber = parsePositiveInteger(argument.substr(lastColon + 1U));
    if (!lastNumber) {
        return result;
    }

    const auto beforeLast = argument.substr(0, lastColon);
    const auto previousColon = beforeLast.rfind(':');
    if (previousColon != std::string_view::npos && previousColon + 1U < beforeLast.size()) {
        const auto previousNumber = parsePositiveInteger(beforeLast.substr(previousColon + 1U));
        if (previousNumber) {
            result.path = beforeLast.substr(0, previousColon);
            result.position.line = *previousNumber;
            result.position.column = *lastNumber;
            result.position.hasLine = true;
            result.position.hasColumn = true;
            return result;
        }
    }

    result.path = beforeLast;
    result.position.line = *lastNumber;
    result.position.hasLine = true;
    return result;
}

[[nodiscard]] LaunchParseResult parseError(std::string message) {
    return {{}, std::move(message)};
}

}

LaunchParseResult LaunchRequestParser::parse(const std::span<const std::string_view> arguments) {
    LaunchRequest request;
    OpenModePreference modePreference = OpenModePreference::Automatic;
    TextPosition pendingPosition{};
    bool optionsEnabled = true;
    bool consumedStdin = false;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments[index];

        if (optionsEnabled && argument == "--") {
            optionsEnabled = false;
            continue;
        }
        if (optionsEnabled && (argument == "--help" || argument == "-h")) {
            request.showHelp = true;
            continue;
        }
        if (optionsEnabled && argument == "--view") {
            modePreference = OpenModePreference::Viewer;
            continue;
        }
        if (optionsEnabled && argument == "--edit") {
            modePreference = OpenModePreference::Editor;
            continue;
        }
        if (optionsEnabled && argument == "--auto") {
            modePreference = OpenModePreference::Automatic;
            continue;
        }
        if (optionsEnabled && (argument == "--line" || argument == "-l")) {
            if (index + 1U >= arguments.size()) {
                return parseError("missing value for --line");
            }
            const auto value = parsePositiveInteger(arguments[++index]);
            if (!value) {
                return parseError("--line expects a positive integer");
            }
            pendingPosition.line = *value;
            pendingPosition.hasLine = true;
            continue;
        }
        if (optionsEnabled && (argument == "--column" || argument == "-c")) {
            if (index + 1U >= arguments.size()) {
                return parseError("missing value for --column");
            }
            const auto value = parsePositiveInteger(arguments[++index]);
            if (!value) {
                return parseError("--column expects a positive integer");
            }
            pendingPosition.column = *value;
            pendingPosition.hasColumn = true;
            continue;
        }
        if (optionsEnabled && argument.size() > 1U && argument.front() == '+' &&
            argument.find_first_not_of("0123456789:", 1U) == std::string_view::npos) {
            const auto separator = argument.find(':', 1U);
            const auto lineText = separator == std::string_view::npos
                                      ? argument.substr(1U)
                                      : argument.substr(1U, separator - 1U);
            const auto line = parsePositiveInteger(lineText);
            if (!line) {
                return parseError("+line expects a positive line number");
            }
            pendingPosition.line = *line;
            pendingPosition.hasLine = true;
            if (separator != std::string_view::npos) {
                const auto column = parsePositiveInteger(argument.substr(separator + 1U));
                if (!column) {
                    return parseError("+line:column expects a positive column number");
                }
                pendingPosition.column = *column;
                pendingPosition.hasColumn = true;
            }
            continue;
        }
        if (optionsEnabled && argument.size() > 1U && argument.front() == '-') {
            return parseError("unknown option: " + std::string(argument));
        }

        if (pendingPosition.hasColumn && !pendingPosition.hasLine) {
            return parseError("a column requires a line number");
        }

        LaunchTarget target;
        target.modePreference = modePreference;

        if (optionsEnabled && argument == "-") {
            if (consumedStdin) {
                return parseError("standard input may only be opened once");
            }
            consumedStdin = true;
            target.inputKind = LaunchInputKind::StandardInput;
            target.position = pendingPosition;
        } else {
            const auto parsedPath = parsePathPosition(argument);
            if (parsedPath.path.empty()) {
                return parseError("file path must not be empty");
            }
            if ((pendingPosition.hasLine || pendingPosition.hasColumn) &&
                (parsedPath.position.hasLine || parsedPath.position.hasColumn)) {
                return parseError("text position specified more than once for a file");
            }

            target.inputKind = LaunchInputKind::File;
            target.path = pathFromUtf8(parsedPath.path);
            target.position = pendingPosition.hasLine || pendingPosition.hasColumn
                                  ? pendingPosition
                                  : parsedPath.position;
        }

        request.targets.push_back(std::move(target));
        pendingPosition = {};
    }

    if (pendingPosition.hasLine || pendingPosition.hasColumn) {
        return parseError("text position was provided without a following input");
    }

    return {std::move(request), {}};
}

std::string_view LaunchRequestParser::usage() noexcept {
    return "notepadFasaFiso [options] [file[:line[:column]] ... | -]\n"
           "  -h, --help       show this help\n"
           "  --auto           choose editor/viewer automatically\n"
           "  --edit           request editor mode\n"
           "  --view           request view mode\n"
           "  -l, --line N     open next input at line N\n"
           "  -c, --column N   open next input at column N\n"
           "  +N[:M]           open next input at line N, column M\n"
           "  -                 read text from standard input\n";
}

}
