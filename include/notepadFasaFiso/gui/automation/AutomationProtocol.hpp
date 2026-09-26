#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nff::gui::automation {

struct AutomationPoint final {
    int x{};
    int y{};

    friend bool operator==(const AutomationPoint&, const AutomationPoint&) = default;
};

struct AutomationRect final {
    int x{};
    int y{};
    int width{};
    int height{};

    friend bool operator==(const AutomationRect&, const AutomationRect&) = default;
};

struct AutomationElementSnapshot final {
    std::string id;
    std::string role;
    AutomationRect bounds{};
    bool visible{};
    bool enabled{};
    bool checked{};
    std::string text;

    friend bool operator==(const AutomationElementSnapshot&,
                           const AutomationElementSnapshot&) = default;
};

struct AutomationWindowSnapshot final {
    std::uint32_t pid{};
    std::uint64_t nativeHandle{};
    AutomationRect outerBounds{};
    AutomationRect clientBounds{};
    std::uint32_t dpi{};
    bool maximized{};
    std::string executablePath;

    friend bool operator==(const AutomationWindowSnapshot&,
                           const AutomationWindowSnapshot&) = default;
};

struct AutomationStatusSnapshot final {
    std::string position;
    std::string encoding;
    std::string lineEnding;
    std::string format;
    std::string mode;
    std::string extra;

    friend bool operator==(const AutomationStatusSnapshot&,
                           const AutomationStatusSnapshot&) = default;
};

enum class BridgeCommandKind {
    Ping,
    Tree,
    Get,
    Status,
    Window,
};

struct BridgeCommand final {
    BridgeCommandKind kind{};
    std::string semanticId;
};

enum class BridgeCommandError {
    UnknownCommand,
    InvalidArgumentCount,
    InvalidSemanticId,
};

enum class ProtocolError {
    InvalidEscape,
    InvalidFieldCount,
    InvalidInteger,
    InvalidBoolean,
    InvalidSemanticId,
    InvalidRectangle,
    InvalidRecordType,
};

[[nodiscard]] std::expected<BridgeCommand, BridgeCommandError>
parseBridgeCommand(std::string_view request);

[[nodiscard]] std::string serializeStatus(const AutomationStatusSnapshot& snapshot);
[[nodiscard]] std::expected<AutomationStatusSnapshot, ProtocolError>
parseStatus(std::string_view record);
[[nodiscard]] std::string serializeWindow(const AutomationWindowSnapshot& snapshot);
[[nodiscard]] std::expected<AutomationWindowSnapshot, ProtocolError>
parseWindow(std::string_view record);

[[nodiscard]] std::string encodeField(std::string_view value);
[[nodiscard]] std::expected<std::string, ProtocolError> decodeField(std::string_view value);

[[nodiscard]] std::string serializeElement(const AutomationElementSnapshot& snapshot);
[[nodiscard]] std::expected<AutomationElementSnapshot, ProtocolError>
parseElement(std::string_view record);

[[nodiscard]] bool isValidSemanticId(std::string_view id) noexcept;
[[nodiscard]] bool isAddressableAutomationId(std::string_view id) noexcept;
[[nodiscard]] bool isAllowedTargetExecutableName(std::string_view filename) noexcept;
[[nodiscard]] std::span<const std::string_view> requiredStaticSemanticIds() noexcept;

[[nodiscard]] bool setElementText(std::span<AutomationElementSnapshot> elements,
                                  std::string_view id,
                                  std::string_view text);

[[nodiscard]] bool elementMatchesWaitState(
    const std::optional<AutomationElementSnapshot>& element,
    std::string_view state) noexcept;

[[nodiscard]] bool pointInside(const AutomationRect& rect, AutomationPoint point) noexcept;
[[nodiscard]] AutomationPoint centerPoint(const AutomationRect& rect) noexcept;
[[nodiscard]] std::vector<AutomationPoint> interpolateDrag(AutomationPoint from,
                                                           AutomationPoint to,
                                                           std::size_t segments);

}
