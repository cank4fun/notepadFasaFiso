#pragma once

#if defined(_WIN32)

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace nff::gui::automation::windows {

class AutomationPipeServer final {
public:
    using RequestHandler = std::function<std::string(std::string_view)>;

    AutomationPipeServer(std::uint32_t pid, RequestHandler handler);
    ~AutomationPipeServer();

    AutomationPipeServer(const AutomationPipeServer&) = delete;
    AutomationPipeServer& operator=(const AutomationPipeServer&) = delete;

    [[nodiscard]] bool start();
    void stop() noexcept;
    [[nodiscard]] const std::wstring& pipeName() const noexcept { return pipeName_; }

private:
    static DWORD WINAPI threadProc(void* context);
    void run();

    std::wstring pipeName_;
    RequestHandler handler_;
    void* stopEvent_{};
    void* threadHandle_{};
};

[[nodiscard]] std::wstring automationPipeName(std::uint32_t pid);

}

#endif
