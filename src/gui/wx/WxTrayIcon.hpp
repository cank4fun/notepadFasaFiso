#pragma once

#include <wx/taskbar.h>

class wxBitmapBundle;
class wxFrame;
class wxMenu;

namespace nff::gui::wxbackend {

class WxTrayIcon final : public wxTaskBarIcon {
public:
    explicit WxTrayIcon(wxFrame& frame);
    ~WxTrayIcon() override;

    [[nodiscard]] bool install(const wxBitmapBundle& icon);

protected:
    [[nodiscard]] wxMenu* CreatePopupMenu() override;

private:
    void onActivate(wxTaskBarIconEvent& event);
    void onShow(wxCommandEvent& event);
    void onQuit(wxCommandEvent& event);

    wxFrame* frame_{};
};

}
