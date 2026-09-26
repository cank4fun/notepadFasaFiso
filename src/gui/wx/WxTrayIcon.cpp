#include "WxTrayIcon.hpp"

#include <wx/bmpbndl.h>
#include <wx/frame.h>
#include <wx/menu.h>

namespace nff::gui::wxbackend {
namespace {

enum : int {
    TrayShow = wxID_HIGHEST + 401,
    TrayQuit,
};

}

WxTrayIcon::WxTrayIcon(wxFrame& frame) : frame_(&frame) {
    Bind(wxEVT_TASKBAR_LEFT_DCLICK, &WxTrayIcon::onActivate, this);
    Bind(wxEVT_MENU, &WxTrayIcon::onShow, this, TrayShow);
    Bind(wxEVT_MENU, &WxTrayIcon::onQuit, this, TrayQuit);
}

WxTrayIcon::~WxTrayIcon() {
    Unbind(wxEVT_TASKBAR_LEFT_DCLICK, &WxTrayIcon::onActivate, this);
    Unbind(wxEVT_MENU, &WxTrayIcon::onShow, this, TrayShow);
    Unbind(wxEVT_MENU, &WxTrayIcon::onQuit, this, TrayQuit);
    static_cast<void>(RemoveIcon());
}

bool WxTrayIcon::install(const wxBitmapBundle& icon) {
    if (!wxTaskBarIcon::IsAvailable() || !icon.IsOk()) {
        return false;
    }
    return SetIcon(icon, "notepadFasaFiso");
}

wxMenu* WxTrayIcon::CreatePopupMenu() {
    auto* menu = new wxMenu();
    menu->Append(TrayShow, "Show");
    menu->AppendSeparator();
    menu->Append(TrayQuit, "Exit");
    return menu;
}

void WxTrayIcon::onActivate(wxTaskBarIconEvent& event) {
    if (frame_ != nullptr) {
        frame_->Show(true);
        frame_->Restore();
        frame_->Raise();
    }
    event.Skip();
}

void WxTrayIcon::onShow(wxCommandEvent& event) {
    if (frame_ != nullptr) {
        frame_->Show(true);
        frame_->Restore();
        frame_->Raise();
    }
    event.Skip();
}

void WxTrayIcon::onQuit(wxCommandEvent& event) {
    if (frame_ != nullptr) {
        frame_->Close();
    }
    event.Skip();
}

}
