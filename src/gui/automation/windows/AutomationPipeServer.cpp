#include "AutomationPipeServer.hpp"

#if defined(_WIN32)

#include <windows.h>
#include <sddl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace nff::gui::automation::windows {
namespace {

constexpr DWORD kRequestLimit = 64U * 1024U;
constexpr std::size_t kResponseLimit = 8U * 1024U * 1024U;

struct LocalFreeDeleter final {
    void operator()(void* value) const noexcept {
        if (value != nullptr) {
            static_cast<void>(::LocalFree(value));
        }
    }
};

using LocalPtr = std::unique_ptr<void, LocalFreeDeleter>;

bool makeCurrentUserSecurity(SECURITY_ATTRIBUTES& attributes, LocalPtr& descriptorStorage) {
    HANDLE token = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE) {
        return false;
    }

    DWORD bytes = 0U;
    static_cast<void>(::GetTokenInformation(token, TokenUser, nullptr, 0U, &bytes));
    if (bytes == 0U) {
        ::CloseHandle(token);
        return false;
    }
    std::vector<std::byte> buffer(bytes);
    if (::GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes) == FALSE) {
        ::CloseHandle(token);
        return false;
    }
    ::CloseHandle(token);

    const auto* tokenUser = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    LPWSTR sidText = nullptr;
    if (::ConvertSidToStringSidW(tokenUser->User.Sid, &sidText) == FALSE) {
        return false;
    }
    LocalPtr sidStorage(sidText);

    std::wstring sddl = L"D:P(A;;GA;;;";
    sddl += sidText;
    sddl += L")";

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (::ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr) == FALSE) {
        return false;
    }
    descriptorStorage.reset(descriptor);
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;
    return true;
}

bool waitOverlapped(HANDLE file, OVERLAPPED& operation, HANDLE stopEvent, DWORD& transferred) {
    const std::array<HANDLE, 2> waits{operation.hEvent, stopEvent};
    const auto result = ::WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(),
                                                 FALSE, INFINITE);
    if (result == WAIT_OBJECT_0 + 1U) {
        static_cast<void>(::CancelIoEx(file, &operation));
        return false;
    }
    if (result != WAIT_OBJECT_0) {
        static_cast<void>(::CancelIoEx(file, &operation));
        return false;
    }
    return ::GetOverlappedResult(file, &operation, &transferred, FALSE) != FALSE;
}

bool readRequest(HANDLE pipe, HANDLE stopEvent, std::string& request) {
    std::array<char, kRequestLimit + 1U> buffer{};
    OVERLAPPED operation{};
    operation.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (operation.hEvent == nullptr) return false;

    DWORD transferred = 0U;
    const BOOL started = ::ReadFile(pipe, buffer.data(), kRequestLimit, nullptr, &operation);
    bool ok = started != FALSE;
    if (!ok && ::GetLastError() == ERROR_IO_PENDING) {
        ok = waitOverlapped(pipe, operation, stopEvent, transferred);
    } else if (ok) {
        ok = ::GetOverlappedResult(pipe, &operation, &transferred, TRUE) != FALSE;
    }
    const auto error = ok ? ERROR_SUCCESS : ::GetLastError();
    ::CloseHandle(operation.hEvent);

    if (!ok || error == ERROR_MORE_DATA || transferred == 0U || transferred > kRequestLimit) {
        return false;
    }
    request.assign(buffer.data(), static_cast<std::size_t>(transferred));
    while (!request.empty() && (request.back() == '\n' || request.back() == '\r')) {
        request.pop_back();
    }
    return !request.empty();
}

bool writeResponse(HANDLE pipe, HANDLE stopEvent, const std::string_view response) {
    if (response.size() > kResponseLimit || response.size() > static_cast<std::size_t>(MAXDWORD)) {
        return false;
    }
    OVERLAPPED operation{};
    operation.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (operation.hEvent == nullptr) return false;

    DWORD transferred = 0U;
    const auto bytes = static_cast<DWORD>(response.size());
    const BOOL started = ::WriteFile(pipe, response.data(), bytes, nullptr, &operation);
    bool ok = started != FALSE;
    if (!ok && ::GetLastError() == ERROR_IO_PENDING) {
        ok = waitOverlapped(pipe, operation, stopEvent, transferred);
    } else if (ok) {
        ok = ::GetOverlappedResult(pipe, &operation, &transferred, TRUE) != FALSE;
    }
    ::CloseHandle(operation.hEvent);
    return ok && transferred == bytes;
}

}

std::wstring automationPipeName(const std::uint32_t pid) {
    return L"\\\\.\\pipe\\notepadFasaFiso-gui-automation-" + std::to_wstring(pid);
}

AutomationPipeServer::AutomationPipeServer(const std::uint32_t pid, RequestHandler handler)
    : pipeName_(automationPipeName(pid)), handler_(std::move(handler)) {}

AutomationPipeServer::~AutomationPipeServer() {
    stop();
}

DWORD WINAPI AutomationPipeServer::threadProc(void* context) {
    auto* server = static_cast<AutomationPipeServer*>(context);
    if (server != nullptr) {
        server->run();
    }
    return 0U;
}

bool AutomationPipeServer::start() {
    if (threadHandle_ != nullptr) return true;
    stopEvent_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEvent_ == nullptr) return false;
    DWORD threadId = 0U;
    threadHandle_ = ::CreateThread(nullptr, 0U, &AutomationPipeServer::threadProc, this, 0U, &threadId);
    if (threadHandle_ == nullptr) {
        ::CloseHandle(static_cast<HANDLE>(stopEvent_));
        stopEvent_ = nullptr;
        return false;
    }
    return true;
}

void AutomationPipeServer::stop() noexcept {
    if (stopEvent_ != nullptr) {
        static_cast<void>(::SetEvent(static_cast<HANDLE>(stopEvent_)));
    }
    if (threadHandle_ != nullptr) {
        static_cast<void>(::WaitForSingleObject(static_cast<HANDLE>(threadHandle_), INFINITE));
        ::CloseHandle(static_cast<HANDLE>(threadHandle_));
        threadHandle_ = nullptr;
    }
    if (stopEvent_ != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(stopEvent_));
        stopEvent_ = nullptr;
    }
}

void AutomationPipeServer::run() {
    SECURITY_ATTRIBUTES security{};
    LocalPtr descriptor;
    if (!makeCurrentUserSecurity(security, descriptor)) {
        return;
    }
    const auto stopEvent = static_cast<HANDLE>(stopEvent_);

    while (::WaitForSingleObject(stopEvent, 0U) != WAIT_OBJECT_0) {
        const auto pipe = ::CreateNamedPipeW(
            pipeName_.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1U, 64U * 1024U, 64U * 1024U, 0U, &security);
        if (pipe == INVALID_HANDLE_VALUE) {
            return;
        }

        OVERLAPPED connect{};
        connect.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (connect.hEvent == nullptr) {
            ::CloseHandle(pipe);
            return;
        }

        BOOL connected = ::ConnectNamedPipe(pipe, &connect);
        if (connected == FALSE) {
            const auto error = ::GetLastError();
            if (error == ERROR_PIPE_CONNECTED) {
                connected = TRUE;
            } else if (error == ERROR_IO_PENDING) {
                DWORD ignored = 0U;
                connected = waitOverlapped(pipe, connect, stopEvent, ignored) ? TRUE : FALSE;
            }
        }
        ::CloseHandle(connect.hEvent);

        if (connected != FALSE) {
            std::string request;
            if (readRequest(pipe, stopEvent, request)) {
                std::string response = handler_ ? handler_(request) : "ERROR\tno_handler";
                static_cast<void>(writeResponse(pipe, stopEvent, response));
                static_cast<void>(::FlushFileBuffers(pipe));
            }
            static_cast<void>(::DisconnectNamedPipe(pipe));
        }
        ::CloseHandle(pipe);
    }
}

}

#endif
