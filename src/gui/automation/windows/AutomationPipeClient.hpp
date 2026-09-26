#pragma once

#if defined(_WIN32)

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace nff::gui::automation::windows {

enum class PipeClientError {
    InvalidRequest,
    PipeUnavailable,
    OpenFailed,
    WriteFailed,
    ReadFailed,
    ResponseTooLarge,
};

[[nodiscard]] std::wstring automationPipeName(std::uint32_t pid);
[[nodiscard]] std::expected<std::string, PipeClientError>
sendAutomationRequest(std::uint32_t pid, std::string_view request, std::uint32_t timeoutMs = 3000U);

}

#endif
