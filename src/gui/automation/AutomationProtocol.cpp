#include "notepadFasaFiso/gui/automation/AutomationProtocol.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <limits>

namespace nff::gui::automation {
namespace {

constexpr std::array<std::string_view, 18> kRequiredStaticIds{
    "window.main",        "menu.file",          "menu.edit",
    "menu.view",          "menu.format",        "sidebar.search",
    "sidebar.tree",       "sidebar.splitter",   "notice.external",
    "notice.reload",      "notice.save_mine",   "notice.save_as",
    "status.position",    "status.encoding",    "status.line_ending",
    "status.format",      "status.mode",        "status.extra",
};

std::vector<std::string_view> splitTabs(const std::string_view value) {
    std::vector<std::string_view> result;
    std::size_t begin = 0U;
    while (begin <= value.size()) {
        const auto end = value.find('\t', begin);
        if (end == std::string_view::npos) {
            result.push_back(value.substr(begin));
            break;
        }
        result.push_back(value.substr(begin, end - begin));
        begin = end + 1U;
    }
    return result;
}

std::expected<int, ProtocolError> parseInt(const std::string_view field) {
    int value{};
    const auto* first = field.data();
    const auto* last = field.data() + field.size();
    const auto [ptr, error] = std::from_chars(first, last, value);
    if (error != std::errc{} || ptr != last) {
        return std::unexpected(ProtocolError::InvalidInteger);
    }
    return value;
}

std::expected<std::uint32_t, ProtocolError> parseU32(const std::string_view field) {
    std::uint32_t value{};
    const auto* first = field.data();
    const auto* last = field.data() + field.size();
    const auto [ptr, error] = std::from_chars(first, last, value);
    if (error != std::errc{} || ptr != last) {
        return std::unexpected(ProtocolError::InvalidInteger);
    }
    return value;
}

std::expected<std::uint64_t, ProtocolError> parseU64(const std::string_view field) {
    std::uint64_t value{};
    const auto* first = field.data();
    const auto* last = field.data() + field.size();
    const auto [ptr, error] = std::from_chars(first, last, value);
    if (error != std::errc{} || ptr != last) {
        return std::unexpected(ProtocolError::InvalidInteger);
    }
    return value;
}

std::expected<bool, ProtocolError> parseBool(const std::string_view field) {
    if (field == "0") {
        return false;
    }
    if (field == "1") {
        return true;
    }
    return std::unexpected(ProtocolError::InvalidBoolean);
}

char asciiLower(const char ch) noexcept {
    if (ch >= 'A' && ch <= 'Z') {
        return static_cast<char>(ch - 'A' + 'a');
    }
    return ch;
}

}

std::expected<BridgeCommand, BridgeCommandError>
parseBridgeCommand(const std::string_view request) {
    const auto separator = request.find(' ');
    const auto verb = separator == std::string_view::npos ? request : request.substr(0U, separator);
    const auto argument = separator == std::string_view::npos ? std::string_view{} : request.substr(separator + 1U);

    const auto noArgument = [&](const BridgeCommandKind kind)
        -> std::expected<BridgeCommand, BridgeCommandError> {
        if (!argument.empty()) {
            return std::unexpected(BridgeCommandError::InvalidArgumentCount);
        }
        return BridgeCommand{kind, {}};
    };

    if (verb == "PING") return noArgument(BridgeCommandKind::Ping);
    if (verb == "TREE") return noArgument(BridgeCommandKind::Tree);
    if (verb == "STATUS") return noArgument(BridgeCommandKind::Status);
    if (verb == "WINDOW") return noArgument(BridgeCommandKind::Window);
    if (verb == "GET") {
        if (argument.empty() || argument.find(' ') != std::string_view::npos) {
            return std::unexpected(BridgeCommandError::InvalidArgumentCount);
        }
        if (!isAddressableAutomationId(argument)) {
            return std::unexpected(BridgeCommandError::InvalidSemanticId);
        }
        return BridgeCommand{BridgeCommandKind::Get, std::string(argument)};
    }
    return std::unexpected(BridgeCommandError::UnknownCommand);
}

std::string serializeStatus(const AutomationStatusSnapshot& snapshot) {
    return "STATUS\t" + encodeField(snapshot.position) + "\t" + encodeField(snapshot.encoding) +
           "\t" + encodeField(snapshot.lineEnding) + "\t" + encodeField(snapshot.format) + "\t" +
           encodeField(snapshot.mode) + "\t" + encodeField(snapshot.extra);
}

std::expected<AutomationStatusSnapshot, ProtocolError> parseStatus(const std::string_view record) {
    const auto fields = splitTabs(record);
    if (fields.size() != 7U) {
        return std::unexpected(ProtocolError::InvalidFieldCount);
    }
    if (fields[0] != "STATUS") {
        return std::unexpected(ProtocolError::InvalidRecordType);
    }
    AutomationStatusSnapshot result;
    auto decodeInto = [&](const std::size_t index, std::string& destination) -> bool {
        const auto decoded = decodeField(fields[index]);
        if (!decoded) return false;
        destination = *decoded;
        return true;
    };
    if (!decodeInto(1U, result.position) || !decodeInto(2U, result.encoding) ||
        !decodeInto(3U, result.lineEnding) || !decodeInto(4U, result.format) ||
        !decodeInto(5U, result.mode) || !decodeInto(6U, result.extra)) {
        return std::unexpected(ProtocolError::InvalidEscape);
    }
    return result;
}

std::string serializeWindow(const AutomationWindowSnapshot& snapshot) {
    return "WINDOW\t" + std::to_string(snapshot.pid) + "\t" +
           std::to_string(snapshot.nativeHandle) + "\t" + std::to_string(snapshot.outerBounds.x) +
           "\t" + std::to_string(snapshot.outerBounds.y) + "\t" +
           std::to_string(snapshot.outerBounds.width) + "\t" +
           std::to_string(snapshot.outerBounds.height) + "\t" +
           std::to_string(snapshot.clientBounds.x) + "\t" +
           std::to_string(snapshot.clientBounds.y) + "\t" +
           std::to_string(snapshot.clientBounds.width) + "\t" +
           std::to_string(snapshot.clientBounds.height) + "\t" + std::to_string(snapshot.dpi) +
           "\t" + (snapshot.maximized ? "1" : "0") + "\t" + encodeField(snapshot.executablePath);
}

std::expected<AutomationWindowSnapshot, ProtocolError> parseWindow(const std::string_view record) {
    const auto fields = splitTabs(record);
    if (fields.size() != 14U) {
        return std::unexpected(ProtocolError::InvalidFieldCount);
    }
    if (fields[0] != "WINDOW") {
        return std::unexpected(ProtocolError::InvalidRecordType);
    }
    const auto pid = parseU32(fields[1]);
    const auto handle = parseU64(fields[2]);
    const auto outerX = parseInt(fields[3]);
    const auto outerY = parseInt(fields[4]);
    const auto outerW = parseInt(fields[5]);
    const auto outerH = parseInt(fields[6]);
    const auto clientX = parseInt(fields[7]);
    const auto clientY = parseInt(fields[8]);
    const auto clientW = parseInt(fields[9]);
    const auto clientH = parseInt(fields[10]);
    const auto dpi = parseU32(fields[11]);
    const auto maximized = parseBool(fields[12]);
    const auto executable = decodeField(fields[13]);
    if (!pid || !handle || !outerX || !outerY || !outerW || !outerH || !clientX || !clientY ||
        !clientW || !clientH || !dpi || !maximized || !executable) {
        if (!executable) return std::unexpected(ProtocolError::InvalidEscape);
        if (!maximized) return std::unexpected(ProtocolError::InvalidBoolean);
        return std::unexpected(ProtocolError::InvalidInteger);
    }
    if (*outerW < 0 || *outerH < 0 || *clientW < 0 || *clientH < 0) {
        return std::unexpected(ProtocolError::InvalidRectangle);
    }
    return AutomationWindowSnapshot{
        .pid = *pid,
        .nativeHandle = *handle,
        .outerBounds = {*outerX, *outerY, *outerW, *outerH},
        .clientBounds = {*clientX, *clientY, *clientW, *clientH},
        .dpi = *dpi,
        .maximized = *maximized,
        .executablePath = *executable,
    };
}

std::string encodeField(const std::string_view value) {
    std::string encoded;
    encoded.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            encoded += "\\\\";
            break;
        case '\t':
            encoded += "\\t";
            break;
        case '\n':
            encoded += "\\n";
            break;
        case '\r':
            encoded += "\\r";
            break;
        default:
            encoded.push_back(ch);
            break;
        }
    }
    return encoded;
}

std::expected<std::string, ProtocolError> decodeField(const std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t index = 0U; index < value.size(); ++index) {
        const char ch = value[index];
        if (ch != '\\') {
            decoded.push_back(ch);
            continue;
        }
        if (index + 1U >= value.size()) {
            return std::unexpected(ProtocolError::InvalidEscape);
        }
        const char escaped = value[++index];
        switch (escaped) {
        case '\\':
            decoded.push_back('\\');
            break;
        case 't':
            decoded.push_back('\t');
            break;
        case 'n':
            decoded.push_back('\n');
            break;
        case 'r':
            decoded.push_back('\r');
            break;
        default:
            return std::unexpected(ProtocolError::InvalidEscape);
        }
    }
    return decoded;
}

std::string serializeElement(const AutomationElementSnapshot& snapshot) {
    return "ELEMENT\t" + encodeField(snapshot.id) + "\t" + encodeField(snapshot.role) + "\t" +
           std::to_string(snapshot.bounds.x) + "\t" + std::to_string(snapshot.bounds.y) + "\t" +
           std::to_string(snapshot.bounds.width) + "\t" + std::to_string(snapshot.bounds.height) +
           "\t" + (snapshot.visible ? "1" : "0") + "\t" +
           (snapshot.enabled ? "1" : "0") + "\t" + (snapshot.checked ? "1" : "0") + "\t" +
           encodeField(snapshot.text);
}

std::expected<AutomationElementSnapshot, ProtocolError> parseElement(const std::string_view record) {
    const auto fields = splitTabs(record);
    if (fields.size() != 11U) {
        return std::unexpected(ProtocolError::InvalidFieldCount);
    }
    if (fields[0] != "ELEMENT") {
        return std::unexpected(ProtocolError::InvalidRecordType);
    }

    const auto id = decodeField(fields[1]);
    const auto role = decodeField(fields[2]);
    const auto x = parseInt(fields[3]);
    const auto y = parseInt(fields[4]);
    const auto width = parseInt(fields[5]);
    const auto height = parseInt(fields[6]);
    const auto visible = parseBool(fields[7]);
    const auto enabled = parseBool(fields[8]);
    const auto checked = parseBool(fields[9]);
    const auto text = decodeField(fields[10]);

    if (!id || !role || !x || !y || !width || !height || !visible || !enabled || !checked ||
        !text) {
        if (!id || !role || !text) {
            return std::unexpected(ProtocolError::InvalidEscape);
        }
        if (!x || !y || !width || !height) {
            return std::unexpected(ProtocolError::InvalidInteger);
        }
        return std::unexpected(ProtocolError::InvalidBoolean);
    }
    if (!isValidSemanticId(*id)) {
        return std::unexpected(ProtocolError::InvalidSemanticId);
    }
    if (*width < 0 || *height < 0) {
        return std::unexpected(ProtocolError::InvalidRectangle);
    }

    return AutomationElementSnapshot{
        .id = *id,
        .role = *role,
        .bounds = AutomationRect{*x, *y, *width, *height},
        .visible = *visible,
        .enabled = *enabled,
        .checked = *checked,
        .text = *text,
    };
}

bool isValidSemanticId(const std::string_view id) noexcept {
    if (id.empty()) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](const unsigned char ch) {
        return (ch >= static_cast<unsigned char>('a') && ch <= static_cast<unsigned char>('z')) ||
               (ch >= static_cast<unsigned char>('A') && ch <= static_cast<unsigned char>('Z')) ||
               (ch >= static_cast<unsigned char>('0') && ch <= static_cast<unsigned char>('9')) ||
               ch == static_cast<unsigned char>('.') || ch == static_cast<unsigned char>('_') ||
               ch == static_cast<unsigned char>('-');
    });
}

bool isAddressableAutomationId(const std::string_view id) noexcept {
    if (!isValidSemanticId(id)) return false;
    if (std::find(kRequiredStaticIds.begin(), kRequiredStaticIds.end(), id) !=
        kRequiredStaticIds.end()) {
        return true;
    }
    if (id == "dialog.settings" || id == "dialog.find") return true;
    if (id.starts_with("settings.") || id.starts_with("find.")) return true;

    const auto numericSuffix = [id](const std::string_view prefix) {
        if (!id.starts_with(prefix) || id.size() == prefix.size()) return false;
        return std::all_of(id.begin() + static_cast<std::ptrdiff_t>(prefix.size()), id.end(),
                           [](const unsigned char ch) {
                               return ch >= static_cast<unsigned char>('0') &&
                                      ch <= static_cast<unsigned char>('9');
                           });
    };
    return numericSuffix("pane.") || numericSuffix("tab.") || numericSuffix("editor.") ||
           numericSuffix("splitter.");
}

bool isAllowedTargetExecutableName(const std::string_view filename) noexcept {
    constexpr std::string_view expected = "notepadfasafiso_gui.exe";
    if (filename.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < filename.size(); ++index) {
        if (asciiLower(filename[index]) != expected[index]) {
            return false;
        }
    }
    return true;
}

std::span<const std::string_view> requiredStaticSemanticIds() noexcept {
    return kRequiredStaticIds;
}

bool setElementText(const std::span<AutomationElementSnapshot> elements,
                    const std::string_view id,
                    const std::string_view text) {
    const auto found = std::find_if(elements.begin(), elements.end(), [&](const auto& element) {
        return element.id == id;
    });
    if (found == elements.end()) {
        return false;
    }
    found->text.assign(text);
    return true;
}

bool elementMatchesWaitState(
    const std::optional<AutomationElementSnapshot>& element,
    const std::string_view state) noexcept {
    if (state == "hidden") {
        return !element || !element->visible;
    }
    if (!element) {
        return false;
    }
    if (state == "visible") return element->visible;
    if (state == "enabled") return element->enabled;
    if (state == "checked") return element->checked;
    return false;
}

bool pointInside(const AutomationRect& rect, const AutomationPoint point) noexcept {
    if (rect.width <= 0 || rect.height <= 0) {
        return false;
    }
    const auto right = static_cast<long long>(rect.x) + static_cast<long long>(rect.width);
    const auto bottom = static_cast<long long>(rect.y) + static_cast<long long>(rect.height);
    return static_cast<long long>(point.x) >= rect.x && static_cast<long long>(point.x) < right &&
           static_cast<long long>(point.y) >= rect.y && static_cast<long long>(point.y) < bottom;
}

AutomationPoint centerPoint(const AutomationRect& rect) noexcept {
    return AutomationPoint{rect.x + rect.width / 2, rect.y + rect.height / 2};
}

std::vector<AutomationPoint> interpolateDrag(const AutomationPoint from,
                                              const AutomationPoint to,
                                              const std::size_t segments) {
    const auto count = std::max<std::size_t>(segments, 2U);
    std::vector<AutomationPoint> result;
    result.reserve(count + 1U);

    const auto dx = static_cast<long long>(to.x) - static_cast<long long>(from.x);
    const auto dy = static_cast<long long>(to.y) - static_cast<long long>(from.y);
    const auto denominator = static_cast<long long>(count);
    for (std::size_t index = 0U; index <= count; ++index) {
        const auto numerator = static_cast<long long>(index);
        const auto x = static_cast<long long>(from.x) + (dx * numerator) / denominator;
        const auto y = static_cast<long long>(from.y) + (dy * numerator) / denominator;
        result.push_back(AutomationPoint{static_cast<int>(x), static_cast<int>(y)});
    }
    result.front() = from;
    result.back() = to;
    return result;
}

}
