#include "WxStartupSplash.hpp"

#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/graphics.h>
#include <wx/pen.h>
#include <wx/utils.h>
#include <wx/app.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace nff::gui::wxbackend {
namespace {

struct GlyphMetrics final {
    double width{4.0};
    double advance{5.0};
};

using Segment = std::pair<gui::Point, gui::Point>;

constexpr std::array<Segment, 14> kSegments{{
    {{0.55, 0.0}, {3.45, 0.0}},
    {{4.0, 0.55}, {4.0, 3.05}},
    {{4.0, 3.95}, {4.0, 6.45}},
    {{0.55, 7.0}, {3.45, 7.0}},
    {{0.0, 3.95}, {0.0, 6.45}},
    {{0.0, 0.55}, {0.0, 3.05}},
    {{0.55, 3.5}, {1.75, 3.5}},
    {{2.25, 3.5}, {3.45, 3.5}},
    {{0.45, 0.55}, {1.75, 3.05}},
    {{3.55, 0.55}, {2.25, 3.05}},
    {{1.75, 3.95}, {0.45, 6.45}},
    {{2.25, 3.95}, {3.55, 6.45}},
    {{2.0, 0.55}, {2.0, 3.05}},
    {{2.0, 3.95}, {2.0, 6.45}},
}};

enum SegmentIndex : unsigned int {
    A = 0U,
    B,
    C,
    D,
    E,
    F,
    G1,
    G2,
    H,
    I,
    J,
    K,
    L,
    M,
};

[[nodiscard]] std::vector<unsigned int> segmentsFor(const char character) {
    switch (character) {
    case 'n': return {F, E, A, B, C};
    case 'o': return {A, B, C, D, E, F};
    case 't': return {A, L, M};
    case 'e': return {A, F, G1, G2, E, D};
    case 'p': return {A, B, F, G1, G2, E};
    case 'a': return {A, B, C, D, E, G1, G2};
    case 'd': return {A, B, C, D, E, F};
    case 'F': return {A, F, E, G1, G2};
    case 'f': return {A, F, E, G1, G2};
    case 's': return {A, F, G1, G2, C, D};
    case 'y': return {H, I, M};
    case 'm': return {F, E, B, C, H, I};
    case 'h': return {F, E, B, C, G1, G2};
    default: return {};
    }
}

[[nodiscard]] GlyphMetrics glyphMetrics(const char character) noexcept {
    switch (character) {
    case 'i': return {1.35, 2.35};
    case 't': return {3.35, 4.25};
    case ' ': return {0.0, 2.35};
    case '\'': return {1.0, 1.9};
    default: return {4.0, 5.0};
    }
}

[[nodiscard]] double textUnits(const std::string_view text) noexcept {
    double result = 0.0;
    for (const char character : text) {
        result += glyphMetrics(character).advance;
    }
    if (!text.empty()) {
        result -= 1.0;
    }
    return std::max(1.0, result);
}

void strokeSegment(wxGraphicsContext& graphics,
                   const Segment& segment,
                   const double x,
                   const double y,
                   const double scale) {
    wxGraphicsPath path = graphics.CreatePath();
    path.MoveToPoint(x + segment.first.x * scale, y + segment.first.y * scale);
    path.AddLineToPoint(x + segment.second.x * scale, y + segment.second.y * scale);
    graphics.StrokePath(path);
}

[[nodiscard]] wxRect displayClientArea() {
    int displayIndex = wxDisplay::GetFromPoint(wxGetMousePosition());
    if (displayIndex == wxNOT_FOUND) {
        displayIndex = 0;
    }
    const unsigned int displayCount = wxDisplay::GetCount();
    if (displayCount == 0U || displayIndex < 0 ||
        static_cast<unsigned int>(displayIndex) >= displayCount) {
        return wxRect(0, 0, 1280, 720);
    }
    return wxDisplay(static_cast<unsigned int>(displayIndex)).GetClientArea();
}

}

WxStartupSplash::WxStartupSplash(std::function<void()> afterFirstPaint)
    : wxFrame(nullptr,
              wxID_ANY,
              "notepadFasaFiso splash",
              wxDefaultPosition,
              wxDefaultSize,
              wxBORDER_NONE | wxFRAME_NO_TASKBAR | wxFRAME_TOOL_WINDOW | wxSTAY_ON_TOP),
      afterFirstPaint_(std::move(afterFirstPaint)),
      animationTimer_(this) {
    const wxRect display = displayClientArea();
    layout_ = gui::startupSplashLayout(
        {static_cast<double>(display.GetWidth()), static_cast<double>(display.GetHeight())});

    const auto width = static_cast<int>(std::lround(layout_.window.width));
    const auto height = static_cast<int>(std::lround(layout_.window.height));
    SetClientSize(wxSize(width, height));
    SetMinClientSize(wxSize(width, height));
    SetMaxClientSize(wxSize(width, height));

    const int x = display.GetX() + std::max(0, (display.GetWidth() - width) / 2);
    const int y = display.GetY() + std::max(0, (display.GetHeight() - height) / 2);
    SetPosition(wxPoint(x, y));

    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(wxColour(4, 4, 6));
    Bind(wxEVT_PAINT, &WxStartupSplash::onPaint, this);
    Bind(wxEVT_TIMER, &WxStartupSplash::onAnimationTimer, this, animationTimer_.GetId());
    animationClock_.Start();
    animationTimer_.Start(16);
}

void WxStartupSplash::onPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(wxColour(4, 4, 6)));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> graphics(wxGraphicsContext::Create(dc));
    if (!graphics) {
        return;
    }

    const auto& panel = layout_.panel;
    graphics->SetBrush(wxBrush(wxColour(5, 5, 7)));
    wxPen borderPen(wxColour(150, 150, 160, 210),
                    static_cast<int>(std::max(1L, std::lround(layout_.strokeWidth))));
    borderPen.SetJoin(wxJOIN_ROUND);
    graphics->SetPen(borderPen);
    graphics->DrawRoundedRectangle(panel.x,
                                   panel.y,
                                   panel.width,
                                   panel.height,
                                   layout_.cornerRadius);

    drawVectorText(*graphics,
                   "notepadFasaFiso",
                   layout_.wordmark,
                   wxColour(247, 247, 250));
    drawVectorText(*graphics,
                   "type some shi'",
                   layout_.tagline,
                   wxColour(210, 210, 220));

    const auto& indicator = layout_.indicator;
    constexpr double kAnimationPeriodMs = 720.0;
    const double phase = std::fmod(static_cast<double>(animationClock_.Time()), kAnimationPeriodMs) /
                         kAnimationPeriodMs;
    const double position = 0.5 - 0.5 * std::cos(phase * 2.0 * 3.14159265358979323846);
    const double pillHeight = std::max(4.0, indicator.height * 0.62);
    const double pillWidth = std::max(pillHeight * 3.2, indicator.width * 0.16);
    const double travel = std::max(0.0, indicator.width - pillWidth);
    const double pillX = indicator.x + travel * position;
    const double pillY = indicator.y + (indicator.height - pillHeight) * 0.5;
    graphics->SetBrush(wxBrush(wxColour(245, 245, 248)));
    graphics->SetPen(*wxTRANSPARENT_PEN);
    graphics->DrawRoundedRectangle(pillX,
                                   pillY,
                                   pillWidth,
                                   pillHeight,
                                   pillHeight * 0.5);

    if (!firstPaintDelivered_) {
        firstPaintDelivered_ = true;
    }
}

void WxStartupSplash::onAnimationTimer(wxTimerEvent&) {
    const auto& indicator = layout_.indicator;
    const int padding = static_cast<int>(std::ceil(std::max(4.0, indicator.height)));
    RefreshRect(wxRect(static_cast<int>(std::floor(indicator.x)) - padding,
                       static_cast<int>(std::floor(indicator.y)) - padding,
                       static_cast<int>(std::ceil(indicator.width)) + padding * 2,
                       static_cast<int>(std::ceil(indicator.height)) + padding * 2),
                false);

    constexpr long kMinimumVisibleMs = 280;
    if (!firstPaintDelivered_ || !afterFirstPaint_ || animationClock_.Time() < kMinimumVisibleMs) {
        return;
    }

    animationTimer_.Stop();
    auto callback = std::move(afterFirstPaint_);
    wxTheApp->CallAfter(std::move(callback));
}

void WxStartupSplash::drawVectorText(wxGraphicsContext& graphics,
                                     const wxString& text,
                                     const gui::Rect& bounds,
                                     const wxColour& colour) const {
    const auto utf8 = text.utf8_str();
    if (utf8.data() == nullptr) {
        return;
    }
    const std::string_view value(utf8.data(), utf8.length());
    if (!gui::startupSplashCanRender(value)) {
        return;
    }

    const double units = textUnits(value);
    const double scale = std::min(bounds.width / units, bounds.height / 7.0);
    const double contentWidth = units * scale;
    const double contentHeight = 7.0 * scale;
    double cursorX = bounds.x + (bounds.width - contentWidth) * 0.5;
    const double originY = bounds.y + (bounds.height - contentHeight) * 0.5;

    wxPen pen(colour, static_cast<int>(std::max(1L, std::lround(scale * 0.50))));
    pen.SetCap(wxCAP_ROUND);
    pen.SetJoin(wxJOIN_ROUND);
    graphics.SetPen(pen);
    graphics.SetBrush(wxBrush(colour));

    for (const char character : value) {
        const GlyphMetrics metrics = glyphMetrics(character);
        if (character == 'i') {
            const double centre = cursorX + metrics.width * scale * 0.5;
            wxGraphicsPath stem = graphics.CreatePath();
            stem.MoveToPoint(centre, originY + 2.05 * scale);
            stem.AddLineToPoint(centre, originY + 6.75 * scale);
            graphics.StrokePath(stem);
            const double dot = std::max(1.5, scale * 0.56);
            graphics.DrawEllipse(centre - dot * 0.5,
                                 originY,
                                 dot,
                                 dot);
        } else if (character == '\'') {
            wxGraphicsPath apostrophe = graphics.CreatePath();
            apostrophe.MoveToPoint(cursorX + 0.70 * scale, originY + 0.10 * scale);
            apostrophe.AddLineToPoint(cursorX + 0.35 * scale, originY + 1.25 * scale);
            graphics.StrokePath(apostrophe);
        } else if (character != ' ') {
            for (const unsigned int segmentIndex : segmentsFor(character)) {
                strokeSegment(graphics,
                              kSegments.at(static_cast<std::size_t>(segmentIndex)),
                              cursorX,
                              originY,
                              scale);
            }
        }
        cursorX += metrics.advance * scale;
    }
}

}
