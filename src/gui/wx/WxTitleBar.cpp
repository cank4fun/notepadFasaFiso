#include "WxTitleBar.hpp"

#include <wx/cursor.h>
#include <wx/dcbuffer.h>
#include <wx/event.h>
#include <wx/frame.h>
#include <wx/image.h>
#include <wx/utils.h>

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif

#include <algorithm>
#include <cstddef>
#include <utility>

namespace nff::gui::wxbackend {
namespace {

[[nodiscard]] wxColour blend(const wxColour& from,
                             const wxColour& to,
                             const double amount) {
    const auto t = std::clamp(amount, 0.0, 1.0);
    const auto component = [t](const unsigned char left, const unsigned char right) {
        const auto value = static_cast<double>(left) +
                           (static_cast<double>(right) - static_cast<double>(left)) * t;
        return static_cast<unsigned char>(std::clamp(value, 0.0, 255.0));
    };
    return wxColour(component(from.Red(), to.Red()),
                    component(from.Green(), to.Green()),
                    component(from.Blue(), to.Blue()));
}

[[nodiscard]] wxString ellipsize(wxDC& dc, const wxString& text, const int width) {
    if (text.empty() || width <= 0) {
        return {};
    }
    if (dc.GetTextExtent(text).GetWidth() <= width) {
        return text;
    }

    const wxString ellipsis = wxString::FromUTF8("…");
    const int ellipsisWidth = dc.GetTextExtent(ellipsis).GetWidth();
    if (ellipsisWidth >= width) {
        return ellipsis;
    }

    std::size_t low = 0U;
    std::size_t high = text.length();
    while (low < high) {
        const auto middle = low + (high - low + 1U) / 2U;
        const auto candidate = text.Left(middle) + ellipsis;
        if (dc.GetTextExtent(candidate).GetWidth() <= width) {
            low = middle;
        } else {
            high = middle - 1U;
        }
    }
    return text.Left(low) + ellipsis;
}

}

WxTitleBar::WxTitleBar(wxFrame& frame,
                       const WxThemePalette& theme,
                       const bool compact)
    : wxPanel(&frame, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxBORDER_NONE | wxWANTS_CHARS),
      frame_(frame),
      theme_(theme),
      compact_(compact) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetCursor(wxCursor(wxCURSOR_ARROW));

    Bind(wxEVT_PAINT, &WxTitleBar::onPaint, this);
    Bind(wxEVT_MOTION, &WxTitleBar::onMotion, this);
    Bind(wxEVT_LEAVE_WINDOW, &WxTitleBar::onLeave, this);
    Bind(wxEVT_LEFT_DOWN, &WxTitleBar::onLeftDown, this);
    Bind(wxEVT_LEFT_UP, &WxTitleBar::onLeftUp, this);
    Bind(wxEVT_LEFT_DCLICK, &WxTitleBar::onLeftDoubleClick, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &WxTitleBar::onCaptureLost, this);
}

void WxTitleBar::applyTheme(const WxThemePalette& theme) {
    theme_ = theme;
    Refresh(false);
}

void WxTitleBar::setCompact(const bool compact) {
    if (compact_ == compact) {
        return;
    }
    compact_ = compact;
    Refresh(false);
}

void WxTitleBar::setTitle(wxString title) {
    if (title_ == title) {
        return;
    }
    title_ = std::move(title);
    Refresh(false);
}

void WxTitleBar::setIcon(wxBitmap icon) {
    icon_ = std::move(icon);
    Refresh(false);
}

void WxTitleBar::setActive(const bool active) {
    if (active_ == active) {
        return;
    }
    active_ = active;
    Refresh(false);
}

void WxTitleBar::setHotTarget(const HitTarget target) {
    if (hot_ == target) {
        return;
    }
    hot_ = target;
    Refresh(false);
}

void WxTitleBar::setPressedTarget(const HitTarget target) {
    if (pressed_ == target) {
        return;
    }
    pressed_ = target;
    Refresh(false);
}

void WxTitleBar::clearInteraction() {
    const bool changed = hot_ != HitTarget::None || pressed_ != HitTarget::None || dragging_;
    hot_ = HitTarget::None;
    pressed_ = HitTarget::None;
    dragging_ = false;
    if (HasCapture()) {
        ReleaseMouse();
    }
    if (changed) {
        Refresh(false);
    }
}

#ifdef __WXMSW__
WXLRESULT WxTitleBar::MSWWindowProc(const WXUINT message,
                                    const WXWPARAM wParam,
                                    const WXLPARAM lParam) {
    if (message == WM_NCHITTEST) {

        return HTTRANSPARENT;
    }
    return wxPanel::MSWWindowProc(message, wParam, lParam);
}
#endif

WxTitleBar::HitTarget WxTitleBar::hitTestScreen(const wxPoint& screenPoint) const {
    return hitTestClient(ScreenToClient(screenPoint));
}

wxRect WxTitleBar::buttonRect(const HitTarget target) const {
    const auto client = GetClientRect();
    const int buttonWidth = FromDIP(compact_ ? 42 : 46);
    const int right = client.GetRight() + 1;
    int index = -1;
    switch (target) {
    case HitTarget::Close:
        index = 0;
        break;
    case HitTarget::Maximize:
        index = 1;
        break;
    case HitTarget::Minimize:
        index = 2;
        break;
    default:
        return {};
    }
    return wxRect(right - buttonWidth * (index + 1), 0, buttonWidth, client.GetHeight());
}

WxTitleBar::HitTarget WxTitleBar::hitTestClient(const wxPoint& point) const {
    if (!GetClientRect().Contains(point)) {
        return HitTarget::None;
    }
    if (buttonRect(HitTarget::Close).Contains(point)) {
        return HitTarget::Close;
    }
    if (buttonRect(HitTarget::Maximize).Contains(point)) {
        return HitTarget::Maximize;
    }
    if (buttonRect(HitTarget::Minimize).Contains(point)) {
        return HitTarget::Minimize;
    }
    return HitTarget::Drag;
}

void WxTitleBar::onPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    const auto client = GetClientRect();
    const auto titleSurface = blend(theme_.window, theme_.chrome, 0.78);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(titleSurface));
    dc.DrawRectangle(client);

    const auto drawButtonBackground = [this, &dc, &titleSurface](const HitTarget target) {
        const auto rect = buttonRect(target);
        if (rect.IsEmpty()) {
            return;
        }
        wxColour background = titleSurface;
        if (target == HitTarget::Close && hot_ == target) {
            background = wxColour(196, 43, 28);
        } else if (hot_ == target) {
            background = blend(titleSurface, theme_.surfaceRaised, 0.42);
        }
        if (pressed_ == target) {
            background = target == HitTarget::Close
                             ? blend(theme_.danger, theme_.window, 0.18)
                             : blend(background, theme_.text, 0.10);
        }
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(background));
        dc.DrawRectangle(rect);
    };

    drawButtonBackground(HitTarget::Minimize);
    drawButtonBackground(HitTarget::Maximize);
    drawButtonBackground(HitTarget::Close);

    const int iconSize = FromDIP(compact_ ? 16 : 18);
    const int leftPadding = FromDIP(compact_ ? 9 : 11);
    if (icon_.IsOk()) {
        const auto scaled = icon_.GetSize() == wxSize(iconSize, iconSize)
                                ? icon_
                                : wxBitmap(icon_.ConvertToImage().Scale(
                                      iconSize, iconSize, wxIMAGE_QUALITY_HIGH));
        const int iconY = std::max(0, (client.GetHeight() - iconSize) / 2);
        dc.DrawBitmap(scaled, leftPadding, iconY, true);
    }

    wxFont titleFont = GetFont();
    titleFont.SetPointSize(std::max(8, titleFont.GetPointSize() - (compact_ ? 1 : 0)));
    dc.SetFont(titleFont);
    dc.SetTextForeground(active_ ? theme_.text : theme_.mutedText);

    const int leftReserve = leftPadding + iconSize + FromDIP(10);
    const auto minimizeRect = buttonRect(HitTarget::Minimize);
    const int rightReserve = std::max(leftReserve, client.GetWidth() - minimizeRect.GetLeft());
    const int textLeft = leftReserve;
    const int textRight = std::max(textLeft, client.GetWidth() - rightReserve);
    const int textWidth = std::max(0, textRight - textLeft);
    const auto displayTitle = ellipsize(dc, title_, textWidth);
    const auto titleExtent = dc.GetTextExtent(displayTitle);
    const int idealX = (client.GetWidth() - titleExtent.GetWidth()) / 2;
    const int titleX = std::clamp(idealX,
                                  textLeft,
                                  std::max(textLeft, textRight - titleExtent.GetWidth()));
    const int titleY = std::max(0, (client.GetHeight() - titleExtent.GetHeight()) / 2);
    dc.DrawText(displayTitle, titleX, titleY);

    const wxColour glyph = active_ ? theme_.text : theme_.mutedText;
    dc.SetPen(wxPen(glyph, FromDIP(1)));
    const auto minimize = buttonRect(HitTarget::Minimize);
    const int minCx = minimize.GetLeft() + minimize.GetWidth() / 2;
    const int minCy = minimize.GetTop() + minimize.GetHeight() / 2;
    const int half = FromDIP(4);
    dc.DrawLine(minCx - half, minCy + FromDIP(3), minCx + half, minCy + FromDIP(3));

    const auto maximize = buttonRect(HitTarget::Maximize);
    const int maxCx = maximize.GetLeft() + maximize.GetWidth() / 2;
    const int maxCy = maximize.GetTop() + maximize.GetHeight() / 2;
    const int box = FromDIP(9);
    if (frame_.IsMaximized()) {
        const int offset = FromDIP(2);
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(maxCx - box / 2 + offset,
                         maxCy - box / 2 - offset,
                         box,
                         box);
        dc.DrawRectangle(maxCx - box / 2 - offset,
                         maxCy - box / 2 + offset,
                         box,
                         box);
    } else {
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(maxCx - box / 2, maxCy - box / 2, box, box);
    }

    const auto close = buttonRect(HitTarget::Close);
    const int closeCx = close.GetLeft() + close.GetWidth() / 2;
    const int closeCy = close.GetTop() + close.GetHeight() / 2;
    const int closeHalf = FromDIP(4);
    dc.DrawLine(closeCx - closeHalf, closeCy - closeHalf,
                closeCx + closeHalf, closeCy + closeHalf);
    dc.DrawLine(closeCx + closeHalf, closeCy - closeHalf,
                closeCx - closeHalf, closeCy + closeHalf);

    const auto separator = blend(theme_.border, titleSurface, 0.62);
    dc.SetPen(wxPen(separator, 1));
    dc.DrawLine(client.GetLeft(), client.GetBottom(), client.GetRight(), client.GetBottom());
}

void WxTitleBar::onMotion(wxMouseEvent& event) {
#ifdef __WXMSW__
    event.Skip();
    return;
#else
    const auto target = hitTestClient(event.GetPosition());
    setHotTarget(target == HitTarget::Drag ? HitTarget::None : target);
    if (dragging_ && event.LeftIsDown() && !frame_.IsMaximized()) {
        const auto now = ClientToScreen(event.GetPosition());
        frame_.Move(dragStartFrame_ + (now - dragStartScreen_));
    }
    event.Skip();
#endif
}

void WxTitleBar::onLeave(wxMouseEvent& event) {
#ifndef __WXMSW__
    if (!dragging_) {
        setHotTarget(HitTarget::None);
    }
#endif
    event.Skip();
}

void WxTitleBar::onLeftDown(wxMouseEvent& event) {
#ifdef __WXMSW__
    event.Skip();
#else
    const auto target = hitTestClient(event.GetPosition());
    if (target == HitTarget::Drag) {
        dragging_ = true;
        dragStartScreen_ = ClientToScreen(event.GetPosition());
        dragStartFrame_ = frame_.GetPosition();
        if (beginNativeMove(event)) {
            dragging_ = false;
        } else if (!HasCapture()) {
            CaptureMouse();
        }
    } else {
        setPressedTarget(target);
        if (!HasCapture()) {
            CaptureMouse();
        }
    }
    event.Skip();
#endif
}

void WxTitleBar::onLeftUp(wxMouseEvent& event) {
#ifdef __WXMSW__
    event.Skip();
#else
    const auto target = hitTestClient(event.GetPosition());
    const auto pressed = pressed_;
    const bool wasDragging = dragging_;
    dragging_ = false;
    setPressedTarget(HitTarget::None);
    if (HasCapture()) {
        ReleaseMouse();
    }
    if (!wasDragging && target == pressed) {
        activate(target);
    }
    event.Skip();
#endif
}

void WxTitleBar::onLeftDoubleClick(wxMouseEvent& event) {
#ifndef __WXMSW__
    if (hitTestClient(event.GetPosition()) == HitTarget::Drag) {
        frame_.Maximize(!frame_.IsMaximized());
        Refresh(false);
    }
#endif
    event.Skip();
}

void WxTitleBar::onCaptureLost(wxMouseCaptureLostEvent& event) {
    dragging_ = false;
    pressed_ = HitTarget::None;
    Refresh(false);
    event.Skip();
}

void WxTitleBar::activate(const HitTarget target) {
    switch (target) {
    case HitTarget::Minimize:
        frame_.Iconize(true);
        break;
    case HitTarget::Maximize:
        frame_.Maximize(!frame_.IsMaximized());
        Refresh(false);
        break;
    case HitTarget::Close:
        frame_.Close();
        break;
    case HitTarget::None:
    case HitTarget::Drag:
        break;
    }
}

bool WxTitleBar::beginNativeMove(const wxMouseEvent& event) {
#ifdef __WXGTK__
    auto* widget = static_cast<GtkWidget*>(frame_.GetHandle());
    if (widget != nullptr && GTK_IS_WINDOW(widget)) {
        const auto screen = ClientToScreen(event.GetPosition());
        gtk_window_begin_move_drag(GTK_WINDOW(widget),
                                   1,
                                   screen.x,
                                   screen.y,
                                   gtk_get_current_event_time());
        return true;
    }
#else
    static_cast<void>(event);
#endif
    return false;
}

}
