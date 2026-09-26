#include "WxAutomationSnapshot.hpp"

#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/toplevel.h>
#include <wx/window.h>
#include <wx/msw/wrapwin.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace nff::gui::wxbackend {
namespace {

std::string utf8(const wxString& value) {
    const auto buffer = value.utf8_str();
    if (buffer.data() == nullptr) {
        return {};
    }
    return {buffer.data(), buffer.length()};
}

std::string roleFor(const std::string_view id) {
    if (id == "window.main" || id.starts_with("dialog.")) return "window";
    if (id.starts_with("menu.")) return "menu_button";
    if (id == "sidebar.search" || id.starts_with("settings.") || id.starts_with("find.")) {
        return "control";
    }
    if (id == "sidebar.tree") return "tree";
    if (id.ends_with("splitter") || id.starts_with("splitter.")) return "splitter";
    if (id.starts_with("notice.")) return "notice";
    if (id.starts_with("status.")) return "status";
    if (id.starts_with("pane.")) return "pane";
    if (id.starts_with("tab.")) return "tab";
    if (id.starts_with("editor.")) return "editor";
    return "control";
}

std::string textFor(wxWindow& window, const std::string_view id) {
    if (id.starts_with("editor.")) {

        return {};
    }
    if (auto* check = dynamic_cast<wxCheckBox*>(&window); check != nullptr) {
        return utf8(check->GetLabel());
    }
    if (auto* button = dynamic_cast<wxButton*>(&window); button != nullptr) {
        return utf8(button->GetLabel());
    }
    if (auto* text = dynamic_cast<wxStaticText*>(&window); text != nullptr) {
        return utf8(text->GetLabel());
    }
    if (auto* choice = dynamic_cast<wxChoice*>(&window); choice != nullptr) {
        return choice->GetSelection() == wxNOT_FOUND ? std::string{}
                                                     : utf8(choice->GetStringSelection());
    }
    if (auto* edit = dynamic_cast<wxTextCtrl*>(&window); edit != nullptr) {
        return utf8(edit->GetValue());
    }
    return {};
}

bool checkedFor(wxWindow& window) {
    if (auto* check = dynamic_cast<wxCheckBox*>(&window); check != nullptr) {
        return check->GetValue();
    }
    return false;
}

automation::AutomationElementSnapshot snapshotFor(wxWindow& window,
                                                   const std::string& id) {
    const auto origin = window.ClientToScreen(wxPoint(0, 0));
    const auto size = window.GetClientSize();
    return automation::AutomationElementSnapshot{
        .id = id,
        .role = roleFor(id),
        .bounds = automation::AutomationRect{origin.x, origin.y,
                                             std::max(0, size.x), std::max(0, size.y)},
        .visible = window.IsShownOnScreen() && size.x > 0 && size.y > 0,
        .enabled = window.IsEnabled(),
        .checked = checkedFor(window),
        .text = textFor(window, id),
    };
}

void collectWindow(wxWindow& window,
                   std::map<std::string, automation::AutomationElementSnapshot>& output) {
    const auto id = utf8(window.GetName());
    if (automation::isAddressableAutomationId(id)) {
        output.insert_or_assign(id, snapshotFor(window, id));
    }

    auto& children = window.GetChildren();
    for (auto node = children.GetFirst(); node != nullptr; node = node->GetNext()) {
        auto* child = node->GetData();
        if (child != nullptr) {
            collectWindow(*child, output);
        }
    }
}

bool belongsTo(wxWindow* window, wxWindow& root) {
    for (auto* current = window; current != nullptr; current = current->GetParent()) {
        if (current == &root) {
            return true;
        }
    }
    return false;
}

std::string wideToUtf8(const std::wstring_view text) {
    if (text.empty()) return {};
    const auto required = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                                 text.data(), static_cast<int>(text.size()),
                                                 nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    const auto written = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                                text.data(), static_cast<int>(text.size()),
                                                result.data(), required, nullptr, nullptr);
    if (written != required) return {};
    return result;
}

}

std::vector<automation::AutomationElementSnapshot>
collectNamedAutomationElements(wxWindow& root) {
    std::map<std::string, automation::AutomationElementSnapshot> byId;
    collectWindow(root, byId);

    for (auto node = wxTopLevelWindows.GetFirst(); node != nullptr; node = node->GetNext()) {
        auto* top = node->GetData();
        if (top != nullptr && top != &root && belongsTo(top, root)) {
            collectWindow(*top, byId);
        }
    }

    std::vector<automation::AutomationElementSnapshot> result;
    result.reserve(byId.size());
    for (auto& [id, snapshot] : byId) {
        static_cast<void>(id);
        result.push_back(std::move(snapshot));
    }
    return result;
}

automation::AutomationWindowSnapshot collectAutomationWindowSnapshot(wxWindow& root) {
    auto hwnd = reinterpret_cast<HWND>(root.GetHandle());
    const auto foreground = ::GetForegroundWindow();
    if (foreground != nullptr) {
        DWORD foregroundPid = 0U;
        static_cast<void>(::GetWindowThreadProcessId(foreground, &foregroundPid));
        if (foregroundPid == ::GetCurrentProcessId()) {
            hwnd = foreground;
        }
    }
    RECT outer{};
    RECT client{};
    POINT clientOrigin{};
    if (hwnd != nullptr) {
        static_cast<void>(::GetWindowRect(hwnd, &outer));
        static_cast<void>(::GetClientRect(hwnd, &client));
        static_cast<void>(::ClientToScreen(hwnd, &clientOrigin));
    }

    std::array<wchar_t, 32768> pathBuffer{};
    const auto pathLength = ::GetModuleFileNameW(nullptr, pathBuffer.data(),
                                                 static_cast<DWORD>(pathBuffer.size()));
    const std::wstring_view path(pathBuffer.data(),
                                 pathLength > 0U && pathLength < pathBuffer.size()
                                     ? static_cast<std::size_t>(pathLength)
                                     : 0U);

    const auto outerWidth = std::max<LONG>(0, outer.right - outer.left);
    const auto outerHeight = std::max<LONG>(0, outer.bottom - outer.top);
    const auto clientWidth = std::max<LONG>(0, client.right - client.left);
    const auto clientHeight = std::max<LONG>(0, client.bottom - client.top);

    return automation::AutomationWindowSnapshot{
        .pid = ::GetCurrentProcessId(),
        .nativeHandle = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(hwnd)),
        .outerBounds = {outer.left, outer.top, static_cast<int>(outerWidth),
                        static_cast<int>(outerHeight)},
        .clientBounds = {clientOrigin.x, clientOrigin.y, static_cast<int>(clientWidth),
                         static_cast<int>(clientHeight)},
        .dpi = hwnd != nullptr ? ::GetDpiForWindow(hwnd) : 0U,
        .maximized = hwnd != nullptr && ::IsZoomed(hwnd) != FALSE,
        .executablePath = wideToUtf8(path),
    };
}

}

#endif
