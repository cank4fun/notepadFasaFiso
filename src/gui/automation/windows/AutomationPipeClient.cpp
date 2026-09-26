#include "AutomationPipeClient.hpp"

#if defined(_WIN32)

#include <windows.h>

#include <array>
#include <cstddef>
#include <string>

namespace nff::gui::automation::windows {
namespace {

constexpr std::size_t kRequestLimit = 64U * 1024U;
constexpr std::size_t kResponseLimit = 8U * 1024U * 1024U;

}

std::wstring automationPipeName(const std::uint32_t pid) {
    return L"\\\\.\\pipe\\notepadFasaFiso-gui-automation-" + std::to_wstring(pid);
}

std::expected<std::string, PipeClientError>
sendAutomationRequest(const std::uint32_t pid,
                      const std::string_view request,
                      const std::uint32_t timeoutMs) {
    if (request.empty() || request.size() > kRequestLimit) {
        return std::unexpected(PipeClientError::InvalidRequest);
    }
    const auto name = automationPipeName(pid);
    if (::WaitNamedPipeW(name.c_str(), timeoutMs) == FALSE) {
        return std::unexpected(PipeClientError::PipeUnavailable);
    }
    const auto pipe = ::CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0U, nullptr,
                                    OPEN_EXISTING, 0U, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        return std::unexpected(PipeClientError::OpenFailed);
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    static_cast<void>(::SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr));

    std::string framed(request);
    framed.push_back('\n');
    DWORD written = 0U;
    const auto writeOk = ::WriteFile(pipe, framed.data(), static_cast<DWORD>(framed.size()),
                                     &written, nullptr) != FALSE &&
                         written == framed.size();
    if (!writeOk) {
        ::CloseHandle(pipe);
        return std::unexpected(PipeClientError::WriteFailed);
    }

    std::string response;
    std::array<char, 64U * 1024U> buffer{};
    for (;;) {
        DWORD read = 0U;
        const auto ok = ::ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()),
                                   &read, nullptr);
        if (read > 0U) {
            if (response.size() + read > kResponseLimit) {
                ::CloseHandle(pipe);
                return std::unexpected(PipeClientError::ResponseTooLarge);
            }
            response.append(buffer.data(), static_cast<std::size_t>(read));
        }
        if (ok != FALSE) break;
        if (::GetLastError() != ERROR_MORE_DATA) {
            ::CloseHandle(pipe);
            return std::unexpected(PipeClientError::ReadFailed);
        }
    }
    ::CloseHandle(pipe);
    return response;
}

}

#endif
