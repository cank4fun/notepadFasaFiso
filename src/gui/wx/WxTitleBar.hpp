#pragma once

#include "WxTheme.hpp"

#include <wx/bitmap.h>
#include <wx/panel.h>

class wxFrame;
class wxMouseCaptureLostEvent;
class wxMouseEvent;
class wxPaintEvent;

namespace nff::gui::wxbackend {

class WxTitleBar final : public wxPanel {
public:
    enum class HitTarget : unsigned char {
        None,
        Drag,
        Minimize,
        Maximize,
        Close,
    };

    WxTitleBar(wxFrame& frame,
               const WxThemePalette& theme,
               bool compact);

    void applyTheme(const WxThemePalette& theme);
    void setCompact(bool compact);
    void setTitle(wxString title);
    void setIcon(wxBitmap icon);
    void setActive(bool active);
    void setHotTarget(HitTarget target);
    void setPressedTarget(HitTarget target);
    void clearInteraction();

    [[nodiscard]] HitTarget hitTestScreen(const wxPoint& screenPoint) const;
    [[nodiscard]] HitTarget pressedTarget() const noexcept { return pressed_; }

#ifdef __WXMSW__
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

private:
    void onPaint(wxPaintEvent& event);
    void onMotion(wxMouseEvent& event);
    void onLeave(wxMouseEvent& event);
    void onLeftDown(wxMouseEvent& event);
    void onLeftUp(wxMouseEvent& event);
    void onLeftDoubleClick(wxMouseEvent& event);
    void onCaptureLost(wxMouseCaptureLostEvent& event);

    [[nodiscard]] wxRect buttonRect(HitTarget target) const;
    [[nodiscard]] HitTarget hitTestClient(const wxPoint& point) const;
    void activate(HitTarget target);
    [[nodiscard]] bool beginNativeMove(const wxMouseEvent& event);

    wxFrame& frame_;
    WxThemePalette theme_;
    wxString title_{"notepadFasaFiso"};
    wxBitmap icon_{};
    HitTarget hot_{HitTarget::None};
    HitTarget pressed_{HitTarget::None};
    bool compact_{true};
    bool active_{true};
    bool dragging_{false};
    wxPoint dragStartScreen_{};
    wxPoint dragStartFrame_{};
};

}
