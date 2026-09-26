#pragma once

#include "notepadFasaFiso/gui/StartupSplash.hpp"

#include <wx/frame.h>
#include <wx/graphics.h>
#include <wx/stopwatch.h>
#include <wx/timer.h>

#include <functional>

namespace nff::gui::wxbackend {

class WxStartupSplash final : public wxFrame {
public:
    explicit WxStartupSplash(std::function<void()> afterFirstPaint);

private:
    void onPaint(wxPaintEvent& event);
    void onAnimationTimer(wxTimerEvent& event);
    void drawVectorText(wxGraphicsContext& graphics,
                        const wxString& text,
                        const gui::Rect& bounds,
                        const wxColour& colour) const;

    gui::StartupSplashLayout layout_{};
    std::function<void()> afterFirstPaint_;
    wxTimer animationTimer_;
    wxStopWatch animationClock_;
    bool firstPaintDelivered_{false};
};

}
