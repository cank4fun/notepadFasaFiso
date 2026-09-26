#include "WxMainFrame.hpp"
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
#include "WxAutomationSnapshot.hpp"
#include "AutomationPipeServer.hpp"
#endif

#include "WxEditorHost.hpp"
#include "WxFileDropTarget.hpp"
#include "WxTitleBar.hpp"
#include "WxTrayIcon.hpp"
#include "NffEmbeddedTrayPng.hpp"

#include "notepadFasaFiso/core/ExportService.hpp"
#include "notepadFasaFiso/core/TextTransform.hpp"
#include "notepadFasaFiso/core/TextUtilities.hpp"
#include "notepadFasaFiso/formats/FormatTransform.hpp"
#include "notepadFasaFiso/gui/FileDrop.hpp"
#include "notepadFasaFiso/gui/GuiPolishModel.hpp"
#include "notepadFasaFiso/gui/ColorWheelModel.hpp"
#include "notepadFasaFiso/gui/SelectionAppearanceModel.hpp"

#include <wx/accel.h>
#include <wx/app.h>
#include <wx/arrstr.h>
#include <wx/bitmap.h>
#include <wx/bmpbndl.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/choicdlg.h>
#include <wx/cursor.h>
#include <wx/dialog.h>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/event.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/treectrl.h>
#include <wx/menu.h>
#include <wx/mstream.h>
#include <wx/msgdlg.h>
#include <wx/numdlg.h>
#include <wx/panel.h>
#include <wx/popupwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>
#include <wx/timer.h>
#include <wx/utils.h>
#include <wx/version.h>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <dwmapi.h>
#endif

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <functional>
#include <iterator>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
#include <ranges>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace nff::gui::wxbackend {
namespace {

[[nodiscard]] long customFrameStyle() noexcept {
#ifdef __WXMSW__

    return wxSYSTEM_MENU | wxRESIZE_BORDER | wxCLIP_CHILDREN;
#elif defined(__WXGTK__)

    return wxNO_BORDER | wxRESIZE_BORDER | wxCLIP_CHILDREN;
#else
    return (wxDEFAULT_FRAME_STYLE &
            ~(wxCAPTION | wxMINIMIZE_BOX | wxMAXIMIZE_BOX | wxCLOSE_BOX | wxSYSTEM_MENU)) |
           wxRESIZE_BORDER | wxCLIP_CHILDREN;
#endif
}

#ifdef __WXMSW__
void applyNativeFrameAppearance(const HWND hwnd,
                                const WxThemePalette& theme) noexcept {
    if (hwnd == nullptr) {
        return;
    }

    const COLORREF borderColor = DWMWA_COLOR_NONE;
    static_cast<void>(::DwmSetWindowAttribute(hwnd,
                                              DWMWA_BORDER_COLOR,
                                              &borderColor,
                                              sizeof(borderColor)));

    constexpr DWORD immersiveDarkModeAttribute = 20U;
    const BOOL useDarkMode = theme.dark ? TRUE : FALSE;
    static_cast<void>(::DwmSetWindowAttribute(hwnd,
                                              immersiveDarkModeAttribute,
                                              &useDarkMode,
                                              sizeof(useDarkMode)));

    constexpr DWORD cornerPreferenceAttribute = 33U;
    constexpr DWORD roundPreference = 2U;
    static_cast<void>(::DwmSetWindowAttribute(hwnd,
                                              cornerPreferenceAttribute,
                                              &roundPreference,
                                              sizeof(roundPreference)));
}

void configureNativeCustomFrame(const HWND hwnd,
                                const WxThemePalette& theme) noexcept {
    if (hwnd == nullptr) {
        return;
    }

    auto style = static_cast<LONG_PTR>(::GetWindowLongPtrW(hwnd, GWL_STYLE));
    style &= ~static_cast<LONG_PTR>(WS_CAPTION);
    style |= static_cast<LONG_PTR>(WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX |
                                   WS_MAXIMIZEBOX);
    ::SetWindowLongPtrW(hwnd, GWL_STYLE, style);

    applyNativeFrameAppearance(hwnd, theme);

    ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                   SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                       SWP_NOACTIVATE);
}

[[nodiscard]] int nativeResizeBorder(const HWND hwnd, const bool horizontal) noexcept {
    const UINT dpi = hwnd != nullptr ? ::GetDpiForWindow(hwnd) : USER_DEFAULT_SCREEN_DPI;
    const int frameMetric = ::GetSystemMetricsForDpi(horizontal ? SM_CXFRAME : SM_CYFRAME, dpi);
    const int paddedBorder = ::GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    return std::max(1, frameMetric + paddedBorder);
}

void insetMaximizedClientRect(const HWND hwnd, RECT& rect) noexcept {
    if (hwnd == nullptr || ::IsZoomed(hwnd) == FALSE) {
        return;
    }

    const int borderX = nativeResizeBorder(hwnd, true);
    const int borderY = nativeResizeBorder(hwnd, false);
    rect.left += borderX;
    rect.right -= borderX;
    rect.top += borderY;
    rect.bottom -= borderY;
}

[[nodiscard]] LRESULT resizeHitTest(const HWND hwnd, const POINT point,
                                    const int border) noexcept {
    if (hwnd == nullptr || ::IsZoomed(hwnd) != FALSE || border <= 0) {
        return HTNOWHERE;
    }

    RECT window{};
    if (::GetWindowRect(hwnd, &window) == FALSE) {
        return HTNOWHERE;
    }

    const bool left = point.x >= window.left && point.x < window.left + border;
    const bool right = point.x < window.right && point.x >= window.right - border;
    const bool top = point.y >= window.top && point.y < window.top + border;
    const bool bottom = point.y < window.bottom && point.y >= window.bottom - border;

    if (top && left) {
        return HTTOPLEFT;
    }
    if (top && right) {
        return HTTOPRIGHT;
    }
    if (bottom && left) {
        return HTBOTTOMLEFT;
    }
    if (bottom && right) {
        return HTBOTTOMRIGHT;
    }
    if (left) {
        return HTLEFT;
    }
    if (right) {
        return HTRIGHT;
    }
    if (top) {
        return HTTOP;
    }
    if (bottom) {
        return HTBOTTOM;
    }
    return HTNOWHERE;
}

[[nodiscard]] wxPoint clientMessageScreenPoint(const HWND hwnd, const LPARAM lParam) noexcept {
    const auto packed = static_cast<LPARAM>(lParam);
    const POINTS packedPoint = MAKEPOINTS(packed);
    POINT point{static_cast<LONG>(packedPoint.x), static_cast<LONG>(packedPoint.y)};
    if (hwnd != nullptr) {
        static_cast<void>(::ClientToScreen(hwnd, &point));
    }
    return wxPoint(static_cast<int>(point.x), static_cast<int>(point.y));
}
#endif

#ifdef __WXGTK__
void applyGtkWindowManagerHints(GtkWidget* widget) noexcept {
    if (widget == nullptr || !GTK_IS_WINDOW(widget) || !gtk_widget_get_realized(widget)) {
        return;
    }
    auto* window = gtk_widget_get_window(widget);
    if (window == nullptr) {
        return;
    }

    gdk_window_set_decorations(window, static_cast<GdkWMDecoration>(0));
    const auto functions = static_cast<GdkWMFunction>(
        GDK_FUNC_MOVE | GDK_FUNC_RESIZE | GDK_FUNC_MINIMIZE |
        GDK_FUNC_MAXIMIZE | GDK_FUNC_CLOSE);
    gdk_window_set_functions(window, functions);
}

void onGtkFrameRealized(GtkWidget* widget, gpointer) {
    applyGtkWindowManagerHints(widget);
}

gboolean onGtkFrameMapped(GtkWidget* widget, GdkEvent*, gpointer) {

    applyGtkWindowManagerHints(widget);
    return FALSE;
}

void configureGtkCustomFrame(wxFrame& frame) noexcept {
    auto* widget = static_cast<GtkWidget*>(frame.GetHandle());
    if (widget == nullptr || !GTK_IS_WINDOW(widget)) {
        return;
    }
    gtk_window_set_decorated(GTK_WINDOW(widget), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(widget), TRUE);
    g_signal_connect_after(widget, "realize", G_CALLBACK(onGtkFrameRealized), nullptr);
    g_signal_connect_after(widget, "map-event", G_CALLBACK(onGtkFrameMapped), nullptr);
    applyGtkWindowManagerHints(widget);
}

enum class GtkResizeZoneKind : int {
    Top,
    Bottom,
    Left,
    Right,
};

[[nodiscard]] GdkWindowEdge gtkResizeEdgeForZone(GtkWidget* widget,
                                                  const GdkEventButton& event,
                                                  const GtkResizeZoneKind kind) noexcept {
    constexpr int cornerExtent = 12;
    const int width = std::max(0, gtk_widget_get_allocated_width(widget));
    const int x = static_cast<int>(event.x);
    if (kind == GtkResizeZoneKind::Top) {
        if (x < cornerExtent) {
            return GDK_WINDOW_EDGE_NORTH_WEST;
        }
        if (x >= width - cornerExtent) {
            return GDK_WINDOW_EDGE_NORTH_EAST;
        }
        return GDK_WINDOW_EDGE_NORTH;
    }
    if (kind == GtkResizeZoneKind::Bottom) {
        if (x < cornerExtent) {
            return GDK_WINDOW_EDGE_SOUTH_WEST;
        }
        if (x >= width - cornerExtent) {
            return GDK_WINDOW_EDGE_SOUTH_EAST;
        }
        return GDK_WINDOW_EDGE_SOUTH;
    }
    return kind == GtkResizeZoneKind::Left ? GDK_WINDOW_EDGE_WEST
                                           : GDK_WINDOW_EDGE_EAST;
}

gboolean onGtkResizeZoneButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer userData) {
    if (widget == nullptr || event == nullptr || event->button != 1) {
        return FALSE;
    }

    auto* topLevel = gtk_widget_get_toplevel(widget);
    if (topLevel == nullptr || !GTK_IS_WINDOW(topLevel)) {
        return FALSE;
    }

    auto* topLevelWindow = gtk_widget_get_window(topLevel);
    if (topLevelWindow == nullptr) {
        return FALSE;
    }

    const auto kind = static_cast<GtkResizeZoneKind>(GPOINTER_TO_INT(userData));
    const auto edge = gtkResizeEdgeForZone(widget, *event, kind);
    gdk_window_begin_resize_drag(topLevelWindow, edge, static_cast<int>(event->button),
                                 static_cast<int>(event->x_root),
                                 static_cast<int>(event->y_root), event->time);
    return TRUE;
}

void configureGtkResizeZone(wxWindow& zone, const GtkResizeZoneKind kind) noexcept {
    auto* widget = static_cast<GtkWidget*>(zone.GetHandle());
    if (widget == nullptr) {
        return;
    }
    gtk_widget_add_events(widget, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(widget, "button-press-event", G_CALLBACK(onGtkResizeZoneButtonPress),
                     GINT_TO_POINTER(static_cast<int>(kind)));
}

[[nodiscard]] std::string gtkCssColour(const wxColour& colour) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string value{"#000000"};
    const auto writeByte = [&value](const std::size_t offset, const unsigned char byte) {
        value[offset] = hex[(byte >> 4U) & 0x0FU];
        value[offset + 1U] = hex[byte & 0x0FU];
    };
    writeByte(1U, colour.Red());
    writeByte(3U, colour.Green());
    writeByte(5U, colour.Blue());
    return value;
}

void applyGtkCssClass(wxWindow& window, const char* cssClass) noexcept {
    auto* widget = static_cast<GtkWidget*>(window.GetHandle());
    if (widget == nullptr || cssClass == nullptr) {
        return;
    }
    gtk_style_context_add_class(gtk_widget_get_style_context(widget), cssClass);
}

void applyGtkThemeCss(const WxThemePalette& theme) noexcept {
    static GtkCssProvider* provider = gtk_css_provider_new();
    static bool installed = false;
    if (provider == nullptr) {
        return;
    }
    if (!installed) {
        if (auto* screen = gdk_screen_get_default(); screen != nullptr) {
            gtk_style_context_add_provider_for_screen(
                screen,
                GTK_STYLE_PROVIDER(provider),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 100U);
            installed = true;
        }
    }

    const auto window = gtkCssColour(theme.window);
    const auto chrome = gtkCssColour(theme.chrome);
    const auto surface = gtkCssColour(theme.surface);
    const auto raised = gtkCssColour(theme.surfaceRaised);
    const auto text = gtkCssColour(theme.text);
    const auto muted = gtkCssColour(theme.mutedText);
    const auto border = gtkCssColour(theme.border);
    const auto accent = gtkCssColour(theme.accent);
    const auto hover = gtkCssColour(theme.accentHover);
    const auto selection = gtkCssColour(theme.selection);

    const std::string css =
        "window, dialog, .background { background-color: " + window + "; color: " + text + "; }\n"
        "headerbar { background-image: none; background-color: " + chrome + "; color: " + text +
        "; border-bottom: 1px solid " + border + "; box-shadow: none; }\n"
        "headerbar label { color: " + text + "; }\n"
        "button { background-image: none; background-color: " + raised + "; color: " + text +
        "; border-color: " + border + "; border-radius: 4px; padding: 4px 8px; box-shadow: none; }\n"
        "button:hover { background-color: " + hover + "; color: " + text + "; }\n"
        "button:checked, button:active { background-color: " + selection + "; color: " + text + "; }\n"
        "button.nff-menu-button { background-image: none; background-color: transparent; "
        "border-color: transparent; padding: 2px 0; box-shadow: none; }\n"
        "button.nff-menu-button:hover { background-color: " + raised + "; color: " + text + "; }\n"
        "entry, spinbutton, combobox button { background-image: none; background-color: " + surface +
        "; color: " + text + "; border-color: " + border + "; box-shadow: none; }\n"
        "entry selection { background-color: " + accent + "; color: " + text + "; }\n"
        "checkbutton, radiobutton, label { color: " + text + "; }\n"
        "menu, .menu { background-color: " + raised + "; color: " + text + "; border-color: " + border + "; }\n"
        "menuitem { background-color: " + raised + "; color: " + text + "; }\n"
        "menuitem:hover { background-color: " + selection + "; color: " + text + "; }\n"
        "menuitem:disabled, menuitem:disabled label { color: " + muted + "; }\n"
        "separator { background-color: " + border + "; }\n";

    GError* error = nullptr;
    gtk_css_provider_load_from_data(provider, css.c_str(), -1, &error);
    if (error != nullptr) {
        g_error_free(error);
    }
}

void configureGtkThemedDialog(wxDialog& dialog,
                              const WxThemePalette& theme,
                              const wxString& title) noexcept {
    applyGtkThemeCss(theme);
    auto* widget = static_cast<GtkWidget*>(dialog.GetHandle());
    if (widget == nullptr || !GTK_IS_WINDOW(widget)) {
        return;
    }

    auto* header = gtk_header_bar_new();
    if (header == nullptr) {
        return;
    }
    const auto utf8 = title.utf8_str();
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), utf8.data() != nullptr ? utf8.data() : "");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(header), FALSE);
    gtk_widget_show(header);
    gtk_window_set_titlebar(GTK_WINDOW(widget), header);
    gtk_style_context_add_class(gtk_widget_get_style_context(widget), "nff-window");
}
#endif

#ifdef __WXMSW__
[[nodiscard]] WxTitleBar::HitTarget titleTargetFromNativeHit(const WXWPARAM hit) noexcept {
    switch (hit) {
    case HTMINBUTTON:
        return WxTitleBar::HitTarget::Minimize;
    case HTMAXBUTTON:
        return WxTitleBar::HitTarget::Maximize;
    case HTCLOSE:
        return WxTitleBar::HitTarget::Close;
    case HTCAPTION:
        return WxTitleBar::HitTarget::Drag;
    default:
        return WxTitleBar::HitTarget::None;
    }
}
#endif

enum : int {
    UiNew = wxID_HIGHEST + 101,
    UiOpen,
    UiSettings,
    UiSidebarChooseFolder,
    UiSidebarReindexFolder,
    UiSidebarClearFolder,
    UiSave,
    UiSaveAs,
    UiSaveCopy,
    UiReload,
    UiReopenAsUtf8,
    UiReopenAsUtf16LE,
    UiReopenAsUtf16BE,
    UiReopenAsUtf32LE,
    UiReopenAsUtf32BE,
    UiReopenAsWindows1252,
    UiReopenAsWindows1254,
    UiCloseView,
    UiClosePane,
    UiUndo,
    UiRedo,
    UiCut,
    UiCopy,
    UiPaste,
    UiSelectAll,
    UiFind,
    UiFindNext,
    UiFindPrevious,
    UiReplace,
    UiGoToLine,
    UiSplitHorizontal,
    UiSplitVertical,
    UiToggleSidebar,
    UiToggleTabs,
    UiToggleWrap,
    UiToggleLineNumbers,
    UiToggleLinks,
    UiSwitchToEditor,
    UiSwitchToViewer,
    UiSwitchToHex,
    UiToggleFollow,
    UiViewerPerformanceAutomatic,
    UiViewerPerformanceFast,
    UiViewerPerformanceMemorySaver,
    UiEncodingUtf8,
    UiEncodingUtf16LE,
    UiEncodingUtf16BE,
    UiEncodingUtf32LE,
    UiEncodingUtf32BE,
    UiEncodingWindows1252,
    UiEncodingWindows1254,
    UiToggleBom,
    UiLineEndingLF,
    UiLineEndingCRLF,
    UiLineEndingCR,
    UiFormatValidate,
    UiFormatPretty,
    UiFormatMinify,
    UiConvertExport,
    UiTextTrimTrailingWhitespace,
    UiTextRemoveEmptyLines,
    UiTextRemoveDuplicateLines,
    UiTextSortAscending,
    UiTextSortDescending,
    UiTextReverseLines,
    UiTextTabsToSpaces,
    UiTextSpacesToTabs,
    UiTextLowercaseAscii,
    UiTextUppercaseAscii,
    UiTextColorChoose,
    UiTextColorClear,
    UiSelectionFontFamilyChoose,
    UiSelectionFontFamilyClear,
    UiSelectionFontSizeChoose,
    UiSelectionFontSizeClear,
    UiSelectionSpoilerSet,
    UiSelectionSpoilerClear,
    UiSelectionAppearanceReset,
    UiThemeSystem,
    UiThemeLight,
    UiThemeDark,
    UiAccentViolet,
    UiAccentBlue,
    UiAccentTeal,
    UiAccentRose,
    UiAccentAmber,
    UiDensityCompact,
    UiDensityComfortable,
    UiChooseFont,
    UiFontSize10,
    UiFontSize11,
    UiFontSize12,
    UiFontSize13,
    UiFontSize14,
    UiFontSize16,
    UiFontSize18,
    UiFontSize20,
    UiTabClose,
    UiTabWordWrap,
    UiTabLineNumbers,
    UiTabChooseFont,
    UiTabFontSize10,
    UiTabFontSize11,
    UiTabFontSize12,
    UiTabFontSize13,
    UiTabFontSize14,
    UiTabFontSize16,
    UiTabFontSize18,
    UiTabFontSize20,
    UiTabResetAppearance,
    UiQuit,
    UiMaintenanceTimer = wxID_HIGHEST + 401,
    UiSidebarSearchTimer,
    UiDragPreviewTimer,
    UiRecentBase = wxID_HIGHEST + 501,
};

[[nodiscard]] int toPixel(const double value) noexcept {
    return static_cast<int>(std::max(0.0, value));
}

bool setBounds(wxWindow* window, const Rect& bounds) {
    if (window == nullptr) {
        return false;
    }
    const auto pixels = pixelBounds(bounds);
    const auto position = window->GetPosition();
    const auto size = window->GetSize();
    if (position.x == pixels.x && position.y == pixels.y &&
        size.GetWidth() == pixels.width && size.GetHeight() == pixels.height) {
        return false;
    }
    window->SetSize(pixels.x, pixels.y, pixels.width, pixels.height, wxSIZE_FORCE);
    return true;
}

bool setPixelBounds(wxWindow* window, const int x, const int y, const int width, const int height) {
    if (window == nullptr) {
        return false;
    }
    const auto position = window->GetPosition();
    const auto size = window->GetSize();
    if (position.x == x && position.y == y && size.GetWidth() == width &&
        size.GetHeight() == height) {
        return false;
    }
    window->SetSize(x, y, width, height, wxSIZE_FORCE);
    return true;
}

[[nodiscard]] std::filesystem::path toPath(const wxString& value) {
    const auto buffer = value.utf8_str();
    if (buffer.data() == nullptr) {
        return {};
    }
    std::u8string utf8;
    utf8.reserve(buffer.length());
    for (std::size_t index = 0U; index < buffer.length(); ++index) {
        utf8.push_back(static_cast<char8_t>(buffer.data()[index]));
    }
    return std::filesystem::path(utf8);
}

[[nodiscard]] wxString fromUtf8(const std::string& value) {
    return wxString::FromUTF8(value.c_str(), value.size());
}

[[nodiscard]] std::string toUtf8(const wxString& value) {
    const auto buffer = value.utf8_str();
    return buffer.data() == nullptr ? std::string{} : std::string(buffer.data(), buffer.length());
}

[[nodiscard]] std::string pathUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::filesystem::path pathFromUtf8(const std::string_view value) {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const char byte : value) {
        utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
    }
    return std::filesystem::path(utf8);
}

class SidebarTreeItemData final : public wxTreeItemData {
public:
    SidebarTreeItemData(std::filesystem::path path, const bool directory, const bool loaded)
        : path(std::move(path)), directory(directory), loaded(loaded) {}

    std::filesystem::path path;
    bool directory{false};
    bool loaded{false};
};

[[nodiscard]] SidebarTreeItemData* sidebarTreeData(wxTreeCtrl* tree,
                                                    const wxTreeItemId& item) noexcept {
    if (tree == nullptr || !item.IsOk()) {
        return nullptr;
    }
    return dynamic_cast<SidebarTreeItemData*>(tree->GetItemData(item));
}

[[nodiscard]] bool sidebarHiddenName(const std::filesystem::path& path) {
    const auto name = pathUtf8(path.filename());
    return !name.empty() && name.front() == '.';
}

[[nodiscard]] bool sidebarExcludedDirectory(const std::filesystem::path& path) {
    auto name = pathUtf8(path.filename());
    std::ranges::transform(name, name.begin(), [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    static constexpr std::array<std::string_view, 7> excluded{
        ".git", ".svn", ".hg", "node_modules", ".cache",
        "$recycle.bin", "system volume information"
    };
    return std::ranges::find(excluded, std::string_view{name}) != excluded.end();
}

[[nodiscard]] std::string sidebarSortKey(const std::filesystem::path& path) {
    auto key = pathUtf8(path.filename());
    std::ranges::transform(key, key.begin(), [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return key;
}

[[nodiscard]] wxColour blendColour(const wxColour& from,
                                   const wxColour& to,
                                   const double amount) {
    const auto t = std::clamp(amount, 0.0, 1.0);
    const auto blend = [t](const unsigned char left, const unsigned char right) {
        const auto value = static_cast<double>(left) +
                           (static_cast<double>(right) - static_cast<double>(left)) * t;
        return static_cast<unsigned char>(std::clamp(value, 0.0, 255.0));
    };
    return wxColour(blend(from.Red(), to.Red()),
                    blend(from.Green(), to.Green()),
                    blend(from.Blue(), to.Blue()));
}

enum class ChromeRuleEdge {
    None,
    Top,
    Bottom,
};

class WxChromeStrip final : public wxPanel {
public:
    WxChromeStrip(wxWindow* parent,
                  const WxThemePalette& theme,
                  const ChromeRuleEdge rule)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
          theme_(theme),
          rule_(rule) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &WxChromeStrip::onPaint, this);
    }

    void applyTheme(const WxThemePalette& theme) {
        theme_ = theme;
        Refresh(false);
    }

private:
    void onPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        const auto client = GetClientRect();
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(theme_.chrome));
        dc.DrawRectangle(client);

        if (client.IsEmpty() || rule_ == ChromeRuleEdge::None) {
            return;
        }

        const auto rule = blendColour(theme_.border, theme_.chrome, 0.48);
        dc.SetPen(wxPen(rule, 1));
        const int y = rule_ == ChromeRuleEdge::Top ? client.GetTop() : client.GetBottom();
        dc.DrawLine(client.GetLeft(), y, client.GetRight(), y);
    }

    WxThemePalette theme_;
    ChromeRuleEdge rule_{ChromeRuleEdge::None};
};

enum class SplitterHandleAxis {
    Vertical,
    Horizontal,
};

class WxSplitterHandle final : public wxPanel {
public:
    WxSplitterHandle(wxWindow* parent,
                     const WxThemePalette& theme,
                     const SplitterHandleAxis axis)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                  wxBORDER_NONE | wxWANTS_CHARS),
          theme_(theme),
          axis_(axis) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &WxSplitterHandle::onPaint, this);
        Bind(wxEVT_ENTER_WINDOW, &WxSplitterHandle::onEnter, this);
        Bind(wxEVT_LEAVE_WINDOW, &WxSplitterHandle::onLeave, this);
    }

    void applyTheme(const WxThemePalette& theme) {
        theme_ = theme;
        Refresh(false);
    }

    void setActive(const bool active) {
        if (active_ == active) {
            return;
        }
        active_ = active;
        Refresh(false);
    }

private:
    void onEnter(wxMouseEvent& event) {
        if (!hovered_) {
            hovered_ = true;
            Refresh(false);
        }
        event.Skip();
    }

    void onLeave(wxMouseEvent& event) {
        if (hovered_) {
            hovered_ = false;
            Refresh(false);
        }
        event.Skip();
    }

    void onPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        const auto client = GetClientRect();
        const auto base = blendColour(theme_.window, theme_.surface, 0.46);
        dc.SetBackground(wxBrush(base));
        dc.Clear();
        if (client.IsEmpty()) {
            return;
        }

        const double visualThickness = active_ || hovered_ ? 2.0 : 1.0;
        const auto indicator = localSplitterDragPreview(
            Rect{0.0, 0.0, static_cast<double>(client.GetWidth()),
                 static_cast<double>(client.GetHeight())},
            Point{}, axis_ == SplitterHandleAxis::Vertical, visualThickness, 0.0);
        const auto pixels = pixelBounds(indicator);
        const auto idle = blendColour(theme_.border, base, 0.38);
        const auto hover = blendColour(theme_.splitter, theme_.accent, 0.42);
        const auto colour = active_ ? theme_.accent : (hovered_ ? hover : idle);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(colour));
        dc.DrawRectangle(pixels.x, pixels.y,
                         std::max(1, pixels.width), std::max(1, pixels.height));
    }

    WxThemePalette theme_;
    SplitterHandleAxis axis_{SplitterHandleAxis::Vertical};
    bool hovered_{false};
    bool active_{false};
};

[[nodiscard]] wxString ellipsizeText(wxDC& dc, const wxString& value, const int maximumWidth) {
    if (maximumWidth <= 0 || value.empty()) {
        return {};
    }
    if (dc.GetTextExtent(value).GetWidth() <= maximumWidth) {
        return value;
    }

    const wxString ellipsis = wxString::FromUTF8("…");
    const auto ellipsisWidth = dc.GetTextExtent(ellipsis).GetWidth();
    if (ellipsisWidth >= maximumWidth) {
        return ellipsis;
    }

    std::size_t low = 0U;
    std::size_t high = value.length();
    while (low < high) {
        const auto middle = low + (high - low + 1U) / 2U;
        const auto candidate = value.Left(middle) + ellipsis;
        if (dc.GetTextExtent(candidate).GetWidth() <= maximumWidth) {
            low = middle;
        } else {
            high = middle - 1U;
        }
    }
    return value.Left(low) + ellipsis;
}

enum class TabDropCue {
    None,
    Before,
    After,
    Pane,
};

class WxPolishedTab final : public wxPanel {
public:
    using DragCallback = std::function<void(wxMouseEvent&)>;
    using ActionCallback = std::function<void()>;
    using ContextCallback = std::function<void(wxMouseEvent&)>;

    WxPolishedTab(wxWindow* parent,
                  wxString title,
                  wxString tooltip,
                  const WxThemePalette& theme,
                  const bool active,
                  const bool modified,
                  const bool conflict,
                  const bool compact,
                  DragCallback drag,
                  ActionCallback close,
                  ContextCallback context)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                  wxBORDER_NONE | wxWANTS_CHARS),
          title_(std::move(title)),
          tooltip_(std::move(tooltip)),
          theme_(theme),
          active_(active),
          modified_(modified),
          conflict_(conflict),
          compact_(compact),
          drag_(std::move(drag)),
          close_(std::move(close)),
          context_(std::move(context)) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetToolTip(tooltip_);
        updateWidth();

        animationTimer_.SetOwner(this);
        Bind(wxEVT_PAINT, &WxPolishedTab::onPaint, this);
        Bind(wxEVT_ENTER_WINDOW, &WxPolishedTab::onEnter, this);
        Bind(wxEVT_LEAVE_WINDOW, &WxPolishedTab::onLeave, this);
        Bind(wxEVT_LEFT_DOWN, &WxPolishedTab::onLeftDown, this);
        Bind(wxEVT_LEFT_UP, &WxPolishedTab::onLeftUp, this);
        Bind(wxEVT_MOUSE_CAPTURE_LOST, &WxPolishedTab::onCaptureLost, this);
        Bind(wxEVT_RIGHT_UP, &WxPolishedTab::onRightUp, this);
        Bind(wxEVT_TIMER, &WxPolishedTab::onAnimation, this);
    }

    ~WxPolishedTab() override {
        animationTimer_.Stop();
    }

    void applyTheme(const WxThemePalette& theme) {
        theme_ = theme;
        Refresh(false);
    }

    [[nodiscard]] bool updatePresentation(wxString title,
                                          wxString tooltip,
                                          const bool active,
                                          const bool modified,
                                          const bool conflict,
                                          const bool compact) {
        const bool geometryChanged = title_ != title || compact_ != compact;
        const bool visualChanged = geometryChanged || active_ != active ||
                                   modified_ != modified || conflict_ != conflict;
        const bool tooltipChanged = tooltip_ != tooltip;
        title_ = std::move(title);
        tooltip_ = std::move(tooltip);
        active_ = active;
        modified_ = modified;
        conflict_ = conflict;
        compact_ = compact;
        if (tooltipChanged) {
            SetToolTip(tooltip_);
        }
        if (geometryChanged) {
            updateWidth();
        }
        if (visualChanged) {
            Refresh(false);
        }
        return geometryChanged;
    }

    void setDropCue(const TabDropCue cue) {
        if (dropCue_ == cue) {
            return;
        }
        dropCue_ = cue;
        Refresh(false);
    }

    void setDragging(const bool dragging) {
        if (dragging_ == dragging) {
            return;
        }
        dragging_ = dragging;
        Refresh(false);
    }

private:
    void updateWidth() {
        const auto extent = GetTextExtent(title_);
        const int horizontalChrome = compact_ ? 46 : 54;
        const int desired =
            std::clamp(extent.GetWidth() + horizontalChrome, 96, compact_ ? 196 : 220);
        SetMinSize(wxSize(desired, -1));
        SetMaxSize(wxSize(desired, -1));
    }

    [[nodiscard]] wxRect closeRect() const {
        const auto client = GetClientRect();
        const int side = std::max(18, client.GetHeight());
        return wxRect(std::max(0, client.GetRight() - side + 1), 0, side, client.GetHeight());
    }

    [[nodiscard]] bool closeVisible() const noexcept {
        return active_ || hoverTarget_ > 0.0 || hoverAmount_ > 0.08;
    }

    void startAnimation() {
        if (!animationTimer_.IsRunning()) {
            animationTimer_.Start(16);
        }
    }

    void onEnter(wxMouseEvent& event) {
        hoverTarget_ = 1.0;
        startAnimation();
        event.Skip();
    }

    void onLeave(wxMouseEvent& event) {
        hoverTarget_ = 0.0;
        startAnimation();
        event.Skip();
    }

    void onAnimation(wxTimerEvent&) {
        constexpr double step = 0.24;
        hoverAmount_ += (hoverTarget_ - hoverAmount_) * step;
        if (std::abs(hoverTarget_ - hoverAmount_) < 0.015) {
            hoverAmount_ = hoverTarget_;
            animationTimer_.Stop();
        }
        Refresh(false);
    }

    void onLeftDown(wxMouseEvent& event) {
        const bool closeHit = closeVisible() && closeRect().Contains(event.GetPosition());
        closePress_.press(closeHit);
        if (closeHit) {
            if (!HasCapture()) {
                CaptureMouse();
            }
            Refresh(false);
            event.Skip(false);
            return;
        }
        if (drag_) {
            drag_(event);
            return;
        }
        event.Skip();
    }

    void onLeftUp(wxMouseEvent& event) {
        if (!closePress_.armed()) {
            event.Skip();
            return;
        }
        const bool commit = closePress_.release(
            closeVisible() && closeRect().Contains(event.GetPosition()));
        if (HasCapture()) {
            ReleaseMouse();
        }
        Refresh(false);
        if (commit && close_) {
            close_();
        }
        event.Skip(false);
    }

    void onCaptureLost(wxMouseCaptureLostEvent& event) {
        closePress_.cancel();
        Refresh(false);
        event.Skip(false);
    }

    void onRightUp(wxMouseEvent& event) {
        if (context_) {
            context_(event);
            return;
        }
        event.Skip();
    }

    void onPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        const auto client = GetClientRect();
        const auto stripSurface = blendColour(theme_.chrome, theme_.surface, 0.55);
        dc.SetBackground(wxBrush(stripSurface));
        dc.Clear();
        dc.SetFont(GetFont());
        if (client.IsEmpty()) {
            return;
        }

        const int insetX = compact_ ? 2 : 3;
        const int insetY = compact_ ? 2 : 3;
        wxRect body = client;
        body.Deflate(insetX, insetY);

        wxColour base = active_ ? theme_.surfaceRaised : stripSurface;
        if (active_) {
            base = blendColour(base, theme_.accent, hoverAmount_ * 0.035);
        } else {
            const auto hoverSurface = blendColour(theme_.surface, theme_.accent, 0.025);
            base = blendColour(base, hoverSurface, hoverAmount_);
        }
        if (dragging_) {
            base = blendColour(base, theme_.accent, 0.08);
        }

        if (body.GetWidth() > 0 && body.GetHeight() > 0) {
            wxColour outline = blendColour(theme_.border, stripSurface, active_ ? 0.12 : 0.42);
            if (hoverAmount_ > 0.02) {
                outline = blendColour(outline, theme_.accent, active_ ? 0.10 : 0.08 * hoverAmount_);
            }
            if (dragging_) {
                outline = blendColour(outline, theme_.accent, 0.54);
            }
            dc.SetPen(wxPen(outline, 1));
            dc.SetBrush(wxBrush(base));
            dc.DrawRoundedRectangle(body, compact_ ? 4 : 5);
        }

        const bool hasMarker = modified_ || conflict_;
        int x = compact_ ? 10 : 12;
        const int centerY = client.GetHeight() / 2;
        if (hasMarker) {
            const auto marker = conflict_ ? theme_.danger : theme_.accent;
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(blendColour(marker, base, 0.48)));
            dc.DrawCircle(x + 3, centerY, compact_ ? 3 : 4);
            dc.SetBrush(wxBrush(marker));
            dc.DrawCircle(x + 3, centerY, compact_ ? 2 : 3);
            x += compact_ ? 12 : 14;
        }

        const bool showClose = closeVisible();
        const auto close = closeRect();
        const int textRight = showClose ? close.GetLeft() - 4
                                        : client.GetRight() - (compact_ ? 9 : 11);
        const int textWidth = std::max(0, textRight - x);
        const auto display = ellipsizeText(dc, title_, textWidth);
        const auto inactiveText = blendColour(theme_.mutedText, theme_.text,
                                              hoverAmount_ * 0.52);
        dc.SetTextForeground(dragging_ ? blendColour(theme_.mutedText, theme_.text, 0.18)
                                      : (active_ ? theme_.text : inactiveText));
        const auto textExtent = dc.GetTextExtent(display);
        dc.DrawText(display, x, std::max(0, centerY - textExtent.GetHeight() / 2));

        if (showClose) {
            const auto pointer = ScreenToClient(wxGetMousePosition());
            const bool closeHover = close.Contains(pointer);
            if (closeHover || closePress_.armed()) {
                auto closeBody = close;
                closeBody.Deflate(compact_ ? 3 : 4, compact_ ? 3 : 4);
                const auto closeSurface = closePress_.armed()
                                              ? blendColour(theme_.danger, theme_.surfaceRaised, 0.18)
                                              : blendColour(theme_.surfaceRaised, theme_.text, 0.06);
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(closeSurface));
                dc.DrawRoundedRectangle(closeBody, compact_ ? 4 : 5);
            }

            const auto closeColour = closePress_.armed()
                                         ? theme_.danger
                                         : (closeHover ? theme_.text : theme_.mutedText);
            dc.SetPen(wxPen(closeColour, 1));
            const int radius = compact_ ? 4 : 5;
            const int cx = close.GetLeft() + close.GetWidth() / 2;
            const int cy = centerY;
            dc.DrawLine(cx - radius, cy - radius, cx + radius, cy + radius);
            dc.DrawLine(cx + radius, cy - radius, cx - radius, cy + radius);
        }

        if (active_ && body.GetWidth() > 0) {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(theme_.accent));
            const int indicatorHeight = 2;
            const int indicatorX = body.GetLeft() + (compact_ ? 5 : 6);
            const int indicatorWidth = std::max(0, body.GetWidth() - (compact_ ? 10 : 12));
            const int indicatorY = std::max(body.GetTop(), body.GetBottom() - indicatorHeight + 1);
            dc.DrawRoundedRectangle(indicatorX, indicatorY,
                                    indicatorWidth, indicatorHeight, 1);
        }

        if (dropCue_ == TabDropCue::Before || dropCue_ == TabDropCue::After) {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(theme_.accent));
            const int markerWidth = 3;
            const int markerX = dropCue_ == TabDropCue::Before
                                    ? 0
                                    : std::max(0, client.GetWidth() - markerWidth);
            dc.DrawRoundedRectangle(markerX, 3, markerWidth,
                                    std::max(1, client.GetHeight() - 6), 1);
        } else if (dropCue_ == TabDropCue::Pane && body.GetWidth() > 0 && body.GetHeight() > 0) {
            dc.SetPen(wxPen(blendColour(theme_.accent, theme_.text, 0.10), 2));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            auto cueBody = body;
            cueBody.Deflate(1, 1);
            dc.DrawRoundedRectangle(cueBody, compact_ ? 4 : 5);
        }
    }

    wxString title_;
    wxString tooltip_;
    WxThemePalette theme_;
    bool active_{false};
    bool modified_{false};
    bool conflict_{false};
    bool compact_{true};
    bool dragging_{false};
    PointerPressLatch closePress_{};
    TabDropCue dropCue_{TabDropCue::None};
    double hoverAmount_{0.0};
    double hoverTarget_{0.0};
    wxTimer animationTimer_{};
    DragCallback drag_{};
    ActionCallback close_{};
    ContextCallback context_{};
};

class WxTabAddButton final : public wxPanel {
public:
    using ActionCallback = std::function<void()>;

    WxTabAddButton(wxWindow* parent,
                   const WxThemePalette& theme,
                   const bool compact,
                   ActionCallback action)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                  wxBORDER_NONE | wxWANTS_CHARS),
          theme_(theme),
          compact_(compact),
          action_(std::move(action)) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        updateWidth();
        SetToolTip("New tab");
        Bind(wxEVT_PAINT, &WxTabAddButton::onPaint, this);
        Bind(wxEVT_ENTER_WINDOW, &WxTabAddButton::onEnter, this);
        Bind(wxEVT_LEAVE_WINDOW, &WxTabAddButton::onLeave, this);
        Bind(wxEVT_LEFT_DOWN, &WxTabAddButton::onLeftDown, this);
        Bind(wxEVT_LEFT_UP, &WxTabAddButton::onLeftUp, this);
        Bind(wxEVT_MOUSE_CAPTURE_LOST, &WxTabAddButton::onCaptureLost, this);
    }

    void applyTheme(const WxThemePalette& theme) {
        theme_ = theme;
        Refresh(false);
    }

    [[nodiscard]] bool setCompact(const bool compact) {
        if (compact_ == compact) {
            return false;
        }
        compact_ = compact;
        updateWidth();
        Refresh(false);
        return true;
    }

private:
    void updateWidth() {
        const int width = compact_ ? 28 : 32;
        SetMinSize(wxSize(width, -1));
        SetMaxSize(wxSize(width, -1));
    }
    void onEnter(wxMouseEvent& event) {
        hovered_ = true;
        Refresh(false);
        event.Skip();
    }

    void onLeave(wxMouseEvent& event) {
        hovered_ = false;
        Refresh(false);
        event.Skip();
    }

    void onLeftDown(wxMouseEvent& event) {
        press_.press(GetClientRect().Contains(event.GetPosition()));
        if (press_.armed() && !HasCapture()) {
            CaptureMouse();
        }
        Refresh(false);
        event.Skip(false);
    }

    void onLeftUp(wxMouseEvent& event) {
        const bool commit = press_.release(GetClientRect().Contains(event.GetPosition()));
        if (HasCapture()) {
            ReleaseMouse();
        }
        Refresh(false);
        if (commit && action_) {
            action_();
        }
        event.Skip(false);
    }

    void onCaptureLost(wxMouseCaptureLostEvent& event) {
        press_.cancel();
        Refresh(false);
        event.Skip(false);
    }

    void onPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        const auto client = GetClientRect();
        const auto stripSurface = blendColour(theme_.chrome, theme_.surface, 0.55);
        dc.SetBackground(wxBrush(stripSurface));
        dc.Clear();

        if (hovered_ || press_.armed()) {
            auto body = client;
            body.Deflate(compact_ ? 3 : 4, compact_ ? 3 : 4);
            dc.SetPen(wxPen(theme_.border));
            dc.SetBrush(wxBrush(blendColour(theme_.surfaceRaised, theme_.accent,
                                            press_.armed() ? 0.12 : 0.05)));
            dc.DrawRoundedRectangle(body, compact_ ? 4 : 5);
        }

        const int cx = client.GetWidth() / 2;
        const int cy = client.GetHeight() / 2;
        const int radius = compact_ ? 5 : 6;
        dc.SetPen(wxPen(hovered_ ? theme_.text : theme_.mutedText, 1));
        dc.DrawLine(cx - radius, cy, cx + radius, cy);
        dc.DrawLine(cx, cy - radius, cx, cy + radius);
    }

    WxThemePalette theme_;
    bool compact_{true};
    bool hovered_{false};
    PointerPressLatch press_{};
    ActionCallback action_{};
};

[[nodiscard]] bool pathWithinScope(const std::filesystem::path& path,
                                   const std::filesystem::path& scope) {
    if (scope.empty()) {
        return true;
    }
    const auto relative = path.lexically_normal().lexically_relative(scope.lexically_normal());
    if (relative.empty()) {
        return path.lexically_normal() == scope.lexically_normal();
    }
    if (relative.is_absolute()) {
        return false;
    }
    const auto first = relative.begin();
    return first == relative.end() || *first != std::filesystem::path{".."};
}

[[nodiscard]] constexpr bool encodingSupportsBom(const encoding::Encoding value) noexcept {
    switch (value) {
    case encoding::Encoding::Utf8:
    case encoding::Encoding::Utf16LE:
    case encoding::Encoding::Utf16BE:
    case encoding::Encoding::Utf32LE:
    case encoding::Encoding::Utf32BE:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr core::LineEndingPolicy lineEndingPolicyFor(
    const core::LineEnding value) noexcept {
    switch (value) {
    case core::LineEnding::LF:
        return core::LineEndingPolicy::LF;
    case core::LineEnding::CRLF:
        return core::LineEndingPolicy::CRLF;
    case core::LineEnding::CR:
        return core::LineEndingPolicy::CR;
    case core::LineEnding::Unknown:
    case core::LineEnding::Mixed:
        return core::LineEndingPolicy::Preserve;
    }
    return core::LineEndingPolicy::Preserve;
}

[[nodiscard]] int showThemedConfirmation(wxWindow* parent,
                                         const WxThemePalette& theme,
                                         const wxString& title,
                                         const wxString& message,
                                         const wxString& affirmativeLabel,
                                         const bool destructive = false) {
    wxDialog dialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
                    wxDEFAULT_DIALOG_STYLE);
    dialog.SetBackgroundColour(theme.surfaceRaised);
    dialog.SetForegroundColour(theme.text);

    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* body = new wxStaticText(&dialog, wxID_ANY, message);
    body->SetForegroundColour(theme.text);
    body->SetBackgroundColour(theme.surfaceRaised);
    body->Wrap(430);
    root->Add(body, 1, wxEXPAND | wxALL, 16);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer(1);
    auto* cancel = new wxButton(&dialog, wxID_NO, "Cancel");
    auto* proceed = new wxButton(&dialog, wxID_YES, affirmativeLabel);
    cancel->SetBackgroundColour(theme.surface);
    cancel->SetForegroundColour(theme.text);
    proceed->SetBackgroundColour(destructive ? theme.danger : theme.accent);
    proceed->SetForegroundColour(theme.text);
    buttons->Add(cancel, 0, wxRIGHT, 8);
    buttons->Add(proceed, 0);
    root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);

    dialog.SetAffirmativeId(wxID_YES);
    dialog.SetEscapeId(wxID_NO);
    if (confirmationDefaultButton() == ConfirmationButtonRole::Cancel) {
        cancel->SetDefault();
    } else {
        proceed->SetDefault();
    }
    dialog.SetSizerAndFit(root);
    const auto fitted = dialog.GetSize();
    dialog.SetMinSize(wxSize(std::max(480, fitted.x), std::max(180, fitted.y)));
    dialog.SetSize(dialog.GetMinSize());
    dialog.CentreOnParent();
    return dialog.ShowModal();
}

[[nodiscard]] wxString formatByteSize(const std::uint64_t bytes) {
    constexpr long double kib = 1024.0L;
    constexpr long double mib = kib * 1024.0L;
    constexpr long double gib = mib * 1024.0L;
    const auto value = static_cast<long double>(bytes);
    if (value >= gib) {
        return wxString::Format("%.2f GiB", static_cast<double>(value / gib));
    }
    if (value >= mib) {
        return wxString::Format("%.1f MiB", static_cast<double>(value / mib));
    }
    if (value >= kib) {
        return wxString::Format("%.1f KiB", static_cast<double>(value / kib));
    }
    return wxString::Format("%llu B", static_cast<unsigned long long>(bytes));
}

[[nodiscard]] bool sameSearchOptions(const search::SearchOptions& left,
                                     const search::SearchOptions& right) noexcept {
    return left.kind == right.kind && left.caseSensitive == right.caseSensitive &&
           left.wholeWord == right.wholeWord && left.wrapAround == right.wrapAround;
}

[[nodiscard]] std::uint64_t nextUtf8Boundary(const std::string_view text,
                                             const std::uint64_t offset) noexcept {
    if (offset >= static_cast<std::uint64_t>(text.size())) {
        return static_cast<std::uint64_t>(text.size());
    }
    auto next = static_cast<std::size_t>(offset) + 1U;
    while (next < text.size() &&
           (static_cast<unsigned char>(text[next]) & 0xC0U) == 0x80U) {
        ++next;
    }
    return static_cast<std::uint64_t>(next);
}

[[nodiscard]] app::StartupPersistentState loadGuiStartup(
    const app::ApplicationPaths& paths) {
    return app::ApplicationStateStore(paths).load();
}

[[nodiscard]] std::uint64_t makeTabsFingerprint(
    const app::ApplicationPresentationSnapshot& presentation) noexcept {
    TabTopologyHasher hasher(presentation.tabsVisible);
    for (const auto& pane : presentation.panes) {

        hasher.add(pane.pane.value, 0U);
        for (const auto& tab : pane.tabs) {
            hasher.add(pane.pane.value, tab.view.value);
        }
    }
    return hasher.value();
}

}

class WxColorWheelPanel final : public wxPanel {
public:
    using ChangeHandler = std::function<void(const wxColour&)>;

    WxColorWheelPanel(wxWindow& parent,
                      const WxThemePalette& theme,
                      ChangeHandler handler)
        : wxPanel(&parent, wxID_ANY, wxDefaultPosition, wxSize(210, 210), wxBORDER_NONE),
          theme_(theme), handler_(std::move(handler)) {
        SetMinSize(wxSize(180, 180));
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &WxColorWheelPanel::onPaint, this);
        Bind(wxEVT_LEFT_DOWN, &WxColorWheelPanel::onLeftDown, this);
        Bind(wxEVT_LEFT_UP, &WxColorWheelPanel::onLeftUp, this);
        Bind(wxEVT_MOTION, &WxColorWheelPanel::onMotion, this);
        Bind(wxEVT_MOUSE_CAPTURE_LOST, &WxColorWheelPanel::onCaptureLost, this);
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            cachedSize_ = wxSize{};
            Refresh(false);
            event.Skip();
        });
    }

    void setColor(const wxColour& color) {
        const auto next = rgbToHsv({static_cast<std::uint8_t>(color.Red()),
                                    static_cast<std::uint8_t>(color.Green()),
                                    static_cast<std::uint8_t>(color.Blue())});
        if (std::abs(next.value - hsv_.value) > 0.0001) {
            cachedValue_ = -1.0;
        }
        hsv_ = next;
        Refresh(false);
    }

private:
    void ensureBitmap() {
        const auto size = GetClientSize();
        if (size.x <= 0 || size.y <= 0) {
            return;
        }
        if (bitmap_.IsOk() && cachedSize_ == size &&
            std::abs(cachedValue_ - hsv_.value) <= 0.0001) {
            return;
        }

        wxImage image(size.x, size.y);
        const double centerX = static_cast<double>(size.x - 1) / 2.0;
        const double centerY = static_cast<double>(size.y - 1) / 2.0;
        const double radius = std::max(1.0, std::min(centerX, centerY) - 4.0);
        for (int y = 0; y < size.y; ++y) {
            for (int x = 0; x < size.x; ++x) {
                const double dx = static_cast<double>(x) - centerX;
                const double dy = static_cast<double>(y) - centerY;
                if (std::hypot(dx, dy) > radius) {
                    image.SetRGB(x, y, theme_.surfaceRaised.Red(),
                                 theme_.surfaceRaised.Green(), theme_.surfaceRaised.Blue());
                    continue;
                }
                const auto hsv = colorWheelPointToHsv(
                    static_cast<double>(x), static_cast<double>(y),
                    centerX, centerY, radius, hsv_.value);
                const auto rgb = hsvToRgb(hsv);
                image.SetRGB(x, y, rgb.red, rgb.green, rgb.blue);
            }
        }
        bitmap_ = wxBitmap(image);
        cachedSize_ = size;
        cachedValue_ = hsv_.value;
    }

    void updateFromPoint(const wxPoint point) {
        const auto size = GetClientSize();
        if (size.x <= 0 || size.y <= 0) {
            return;
        }
        const double centerX = static_cast<double>(size.x - 1) / 2.0;
        const double centerY = static_cast<double>(size.y - 1) / 2.0;
        const double radius = std::max(1.0, std::min(centerX, centerY) - 4.0);
        hsv_ = colorWheelPointToHsv(point.x, point.y, centerX, centerY, radius, hsv_.value);
        const auto rgb = hsvToRgb(hsv_);
        Refresh(false);
        if (handler_) {
            handler_(wxColour(rgb.red, rgb.green, rgb.blue));
        }
    }

    void onPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(theme_.surfaceRaised));
        dc.Clear();
        ensureBitmap();
        if (bitmap_.IsOk()) {
            dc.DrawBitmap(bitmap_, 0, 0, false);
        }

        const auto size = GetClientSize();
        const double centerX = static_cast<double>(size.x - 1) / 2.0;
        const double centerY = static_cast<double>(size.y - 1) / 2.0;
        const double radius = std::max(1.0, std::min(centerX, centerY) - 4.0);
        const double radians = hsv_.hue * std::numbers::pi / 180.0;
        const double markerRadius = radius * hsv_.saturation;
        const int markerX = static_cast<int>(std::lround(centerX + std::cos(radians) * markerRadius));
        const int markerY = static_cast<int>(std::lround(centerY - std::sin(radians) * markerRadius));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);

        dc.SetPen(wxPen(theme_.window, 3));
        dc.DrawCircle(markerX, markerY, 6);
        dc.SetPen(wxPen(theme_.text, 1));
        dc.DrawCircle(markerX, markerY, 5);
    }

    void onLeftDown(wxMouseEvent& event) {
        dragging_.begin();
        if (!HasCapture()) CaptureMouse();
        updateFromPoint(event.GetPosition());
    }
    void onLeftUp(wxMouseEvent& event) {
        if (dragging_.active()) updateFromPoint(event.GetPosition());
        dragging_.end();
        if (HasCapture()) ReleaseMouse();
    }
    void onMotion(wxMouseEvent& event) {
        if (dragging_.active() && event.LeftIsDown()) updateFromPoint(event.GetPosition());
        event.Skip();
    }
    void onCaptureLost(wxMouseCaptureLostEvent& event) {
        dragging_.cancel();
        event.Skip(false);
    }

    WxThemePalette theme_{};
    ChangeHandler handler_{};
    HsvColor hsv_{};
    wxBitmap bitmap_{};
    wxSize cachedSize_{};
    double cachedValue_{-1.0};
    PointerDragLatch dragging_{};
};

class WxTextColorDialog final : public wxDialog {
public:
    WxTextColorDialog(wxWindow& parent,
                      const WxThemePalette& theme,
                      const wxColour& initial,
                      wxString headingText = "Choose a color for the selected text")
        : wxDialog(&parent, wxID_ANY, "Text Color", wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE),
          theme_(theme) {
        SetBackgroundColour(theme_.surfaceRaised);
        SetForegroundColour(theme_.text);

        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* heading = new wxStaticText(this, wxID_ANY, std::move(headingText));
        heading->SetForegroundColour(theme_.text);
        heading->SetBackgroundColour(theme_.surfaceRaised);
        root->Add(heading, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 14);

        wheel_ = new WxColorWheelPanel(*this, theme_, [this](const wxColour& color) {
            if (updating_) return;
            updating_ = true;
            red_->SetValue(static_cast<int>(color.Red()));
            green_->SetValue(static_cast<int>(color.Green()));
            blue_->SetValue(static_cast<int>(color.Blue()));
            updating_ = false;
            updatePreview(false);
        });
        root->Add(wheel_, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 12);

        auto* swatches = new wxBoxSizer(wxHORIZONTAL);
        struct Swatch final { const char* name; unsigned char r; unsigned char g; unsigned char b; };
        constexpr std::array presets{
            Swatch{"Red", 230, 90, 100}, Swatch{"Orange", 235, 145, 70},
            Swatch{"Yellow", 225, 190, 70}, Swatch{"Green", 95, 190, 110},
            Swatch{"Teal", 65, 185, 175}, Swatch{"Blue", 80, 145, 235},
            Swatch{"Violet", 145, 105, 235}, Swatch{"Rose", 225, 95, 145},
            Swatch{"Gray", 165, 165, 175}, Swatch{"White", 235, 235, 240},
        };
        for (const auto& preset : presets) {
            const wxColour color(preset.r, preset.g, preset.b);
            auto* frame = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(32, 28),
                                      wxBORDER_NONE);
            frame->SetMinSize(wxSize(32, 28));
            frame->SetBackgroundColour(theme_.border);
            auto* frameSizer = new wxBoxSizer(wxVERTICAL);
            auto* button = new wxButton(frame, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                        wxSize(28, 24), wxBORDER_NONE);
            button->SetBackgroundColour(color);
            button->SetToolTip(preset.name);
            button->SetName(preset.name);
            button->Bind(wxEVT_BUTTON, [this, color](wxCommandEvent&) { setColor(color); });
            frameSizer->Add(button, 1, wxEXPAND | wxALL, 2);
            frame->SetSizer(frameSizer);
            swatchFrames_.push_back({frame, color});
            swatches->Add(frame, 0, wxRIGHT, 5);
        }
        root->Add(swatches, 0, wxEXPAND | wxALL, 14);

        auto* values = new wxFlexGridSizer(2, 8, 10);
        values->AddGrowableCol(1, 1);
        preview_ = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(120, 34),
                               wxBORDER_NONE);
        preview_->SetMinSize(wxSize(120, 34));
        preview_->SetBackgroundStyle(wxBG_STYLE_PAINT);
        preview_->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxPaintDC dc(preview_);
            dc.SetPen(wxPen(theme_.border));
            dc.SetBrush(wxBrush(color()));
            const auto bounds = preview_->GetClientRect();
            dc.DrawRectangle(bounds);
        });
        auto* previewLabel = new wxStaticText(this, wxID_ANY, "Preview");
        previewLabel->SetForegroundColour(theme_.mutedText);
        previewLabel->SetBackgroundColour(theme_.surfaceRaised);
        values->Add(previewLabel, 0, wxALIGN_CENTER_VERTICAL);
        values->Add(preview_, 1, wxEXPAND);

        auto addChannel = [this, values](const wxString& label, wxSpinCtrl*& control) {
            auto* caption = new wxStaticText(this, wxID_ANY, label);
            caption->SetForegroundColour(theme_.mutedText);
            caption->SetBackgroundColour(theme_.surfaceRaised);
            control = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                     wxDefaultSize, wxSP_ARROW_KEYS, 0, 255, 0);
            control->SetBackgroundColour(theme_.surface);
            control->SetForegroundColour(theme_.text);
            control->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { updatePreview(true); });
            control->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { updatePreview(true); });
            values->Add(caption, 0, wxALIGN_CENTER_VERTICAL);
            values->Add(control, 1, wxEXPAND);
        };
        addChannel("Red", red_);
        addChannel("Green", green_);
        addChannel("Blue", blue_);
        root->Add(values, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->AddStretchSpacer(1);
        auto* cancel = new wxButton(this, wxID_CANCEL, "Cancel");
        auto* ok = new wxButton(this, wxID_OK, "Apply Color");
        cancel->SetBackgroundColour(theme_.surface);
        cancel->SetForegroundColour(theme_.text);
        ok->SetBackgroundColour(theme_.accent);
        ok->SetForegroundColour(theme_.text);
        buttons->Add(cancel, 0, wxRIGHT, 8);
        buttons->Add(ok, 0);
        root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

        SetSizerAndFit(root);
        SetMinSize(wxSize(430, GetSize().y));
        setColor(initial.IsOk() ? initial : theme_.text);
        CentreOnParent();
    }

    [[nodiscard]] wxColour color() const {
        return wxColour(static_cast<unsigned char>(red_->GetValue()),
                        static_cast<unsigned char>(green_->GetValue()),
                        static_cast<unsigned char>(blue_->GetValue()));
    }

private:
    void setColor(const wxColour& color) {
        updating_ = true;
        red_->SetValue(static_cast<int>(color.Red()));
        green_->SetValue(static_cast<int>(color.Green()));
        blue_->SetValue(static_cast<int>(color.Blue()));
        if (wheel_ != nullptr) wheel_->setColor(color);
        updating_ = false;
        updatePreview(false);
    }

    void updatePreview(const bool synchronizeWheel) {
        if (preview_ == nullptr || red_ == nullptr || green_ == nullptr || blue_ == nullptr ||
            updating_) {
            return;
        }
        if (synchronizeWheel && wheel_ != nullptr) {
            updating_ = true;
            wheel_->setColor(color());
            updating_ = false;
        }
        refreshSwatchSelection();
        preview_->Refresh(false);
    }

    void refreshSwatchSelection() {
        const auto selected = color();
        std::optional<std::size_t> next;
        for (std::size_t index = 0U; index < swatchFrames_.size(); ++index) {
            if (selected == swatchFrames_[index].second) {
                next = index;
                break;
            }
        }
        if (next == selectedSwatch_) {
            return;
        }
        const auto refreshFrame = [this](const std::optional<std::size_t> index,
                                         const wxColour& background) {
            if (!index || *index >= swatchFrames_.size()) {
                return;
            }
            auto* frame = swatchFrames_[*index].first;
            if (frame != nullptr) {
                frame->SetBackgroundColour(background);
                frame->Refresh(false);
            }
        };
        refreshFrame(selectedSwatch_, theme_.border);
        refreshFrame(next, theme_.accent);
        selectedSwatch_ = next;
    }

    WxThemePalette theme_{};
    WxColorWheelPanel* wheel_{};
    wxPanel* preview_{};
    wxSpinCtrl* red_{};
    wxSpinCtrl* green_{};
    wxSpinCtrl* blue_{};
    bool updating_{false};
    std::optional<std::size_t> selectedSwatch_{};
    std::vector<std::pair<wxPanel*, wxColour>> swatchFrames_{};
};

class WxSearchDialog final : public wxDialog {
public:
    enum class Action : std::uint8_t {
        Previous,
        Next,
        Replace,
        ReplaceAll,
    };

    struct Values final {
        std::string pattern;
        std::string replacement;
        search::SearchOptions options{};
    };

    using Handler = std::function<void(Action, const Values&)>;

    WxSearchDialog(wxWindow& parent,
                   const WxThemePalette& theme,
                   Handler handler)
        : wxDialog(&parent, wxID_ANY, "Find", wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          handler_(std::move(handler)) {
        SetBackgroundColour(theme.surfaceRaised);
        SetForegroundColour(theme.text);
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
        SetName("dialog.find");
#endif

        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* fields = new wxFlexGridSizer(2, 8, 8);
        fields->AddGrowableCol(1, 1);

        auto* findLabel = new wxStaticText(this, wxID_ANY, "Find:");
        findLabel->SetForegroundColour(theme.text);
        find_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxSize(320, -1), wxTE_PROCESS_ENTER);
        find_->SetBackgroundColour(theme.surface);
        find_->SetForegroundColour(theme.text);
        fields->Add(findLabel, 0, wxALIGN_CENTER_VERTICAL);
        fields->Add(find_, 1, wxEXPAND);

        replaceLabel_ = new wxStaticText(this, wxID_ANY, "Replace:");
        replaceLabel_->SetForegroundColour(theme.text);
        replace_ = new wxTextCtrl(this, wxID_ANY);
        replace_->SetBackgroundColour(theme.surface);
        replace_->SetForegroundColour(theme.text);
        fields->Add(replaceLabel_, 0, wxALIGN_CENTER_VERTICAL);
        fields->Add(replace_, 1, wxEXPAND);
        root->Add(fields, 0, wxEXPAND | wxALL, 12);

        auto* options = new wxBoxSizer(wxHORIZONTAL);
        matchCase_ = new wxCheckBox(this, wxID_ANY, "Match case");
        wholeWord_ = new wxCheckBox(this, wxID_ANY, "Whole word");
        regex_ = new wxCheckBox(this, wxID_ANY, "Regex");
        wrap_ = new wxCheckBox(this, wxID_ANY, "Wrap");
        for (auto* control : {matchCase_, wholeWord_, regex_, wrap_}) {
            control->SetForegroundColour(theme.text);
            control->SetBackgroundColour(theme.surfaceRaised);
            options->Add(control, 0, wxRIGHT, 12);
        }
        root->Add(options, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

        status_ = new wxStaticText(this, wxID_ANY, "Ready");
        status_->SetForegroundColour(theme.mutedText);
        status_->SetBackgroundColour(theme.surfaceRaised);

        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        auto* previous = new wxButton(this, wxID_ANY, "Previous");
        auto* next = new wxButton(this, wxID_ANY, "Next");
        replaceButton_ = new wxButton(this, wxID_ANY, "Replace");
        replaceAllButton_ = new wxButton(this, wxID_ANY, "Replace All");
        auto* close = new wxButton(this, wxID_CANCEL, "Close");
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
        find_->SetName("find.pattern");
        replace_->SetName("find.replacement");
        matchCase_->SetName("find.match_case");
        wholeWord_->SetName("find.whole_word");
        regex_->SetName("find.regex");
        wrap_->SetName("find.wrap");
        status_->SetName("find.status");
        previous->SetName("find.previous");
        next->SetName("find.next");
        replaceButton_->SetName("find.replace");
        replaceAllButton_->SetName("find.replace_all");
        close->SetName("find.close");
#endif
        next->SetBackgroundColour(theme.accent);
        next->SetForegroundColour(theme.text);
        buttons->Add(status_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
        buttons->Add(previous, 0, wxRIGHT, 6);
        buttons->Add(next, 0, wxRIGHT, 10);
        buttons->Add(replaceButton_, 0, wxRIGHT, 6);
        buttons->Add(replaceAllButton_, 0, wxRIGHT, 10);
        buttons->Add(close, 0);
        root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

        SetSizer(root);
        SetMinSize(wxSize(480, -1));
        setReplaceMode(false);

        previous->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { fire(Action::Previous); });
        next->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { fire(Action::Next); });
        replaceButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { fire(Action::Replace); });
        replaceAllButton_->Bind(wxEVT_BUTTON,
                                [this](wxCommandEvent&) { fire(Action::ReplaceAll); });
        close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Hide(); });
        find_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { fire(Action::Next); });
        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
            if (event.CanVeto()) {
                Hide();
                event.Veto();
            } else {
                event.Skip();
            }
        });
    }

    void setReplaceMode(const bool enabled) {
        SetTitle(enabled ? "Find / Replace" : "Find");
        replaceLabel_->Show(enabled);
        replace_->Show(enabled);
        replaceButton_->Show(enabled);
        replaceAllButton_->Show(enabled);
        Layout();
        Fit();
    }

    void setValues(const std::string& pattern,
                   const std::string& replacement,
                   const search::SearchOptions& options) {
        find_->ChangeValue(fromUtf8(pattern));
        replace_->ChangeValue(fromUtf8(replacement));
        matchCase_->SetValue(options.caseSensitive);
        wholeWord_->SetValue(options.wholeWord);
        regex_->SetValue(options.kind == search::SearchKind::RegularExpression);
        wrap_->SetValue(options.wrapAround);
    }

    void setStatus(const wxString& value) {
        status_->SetLabel(value);
        Layout();
    }

    void focusFind() {
        find_->SetFocus();
        find_->SelectAll();
    }

private:
    [[nodiscard]] Values values() const {
        Values value;
        value.pattern = toUtf8(find_->GetValue());
        value.replacement = toUtf8(replace_->GetValue());
        value.options.kind = regex_->GetValue() ? search::SearchKind::RegularExpression
                                                : search::SearchKind::Literal;
        value.options.caseSensitive = matchCase_->GetValue();
        value.options.wholeWord = wholeWord_->GetValue();
        value.options.wrapAround = wrap_->GetValue();
        return value;
    }

    void fire(const Action action) {
        if (handler_) {
            handler_(action, values());
        }
    }

    Handler handler_;
    wxTextCtrl* find_{};
    wxStaticText* replaceLabel_{};
    wxTextCtrl* replace_{};
    wxCheckBox* matchCase_{};
    wxCheckBox* wholeWord_{};
    wxCheckBox* regex_{};
    wxCheckBox* wrap_{};
    wxStaticText* status_{};
    wxButton* replaceButton_{};
    wxButton* replaceAllButton_{};
};

WxMainFrame::WxMainFrame(app::LaunchRequest startupRequest
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
                             , std::optional<std::filesystem::path> automationStateRoot
#endif
                             )
    : wxFrame(nullptr,
              wxID_ANY,
              "notepadFasaFiso",
              wxDefaultPosition,
              wxSize(1180, 760),
              customFrameStyle()),
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
      applicationPaths_(automationStateRoot.has_value()
                            ? app::ApplicationPaths::under(std::move(*automationStateRoot))
                            : app::ApplicationPaths::systemDefault()),
#else
      applicationPaths_(app::ApplicationPaths::systemDefault()),
#endif
      startupRequest_(std::move(startupRequest)),
      lifecycle_(app::ApplicationStateStore(applicationPaths_),
                 loadGuiStartup(applicationPaths_),
                 documents_,
                 workspace_),
      settings_(lifecycle_.settings()),
      metadataStore_(applicationPaths_.metadataDirectory),
      presentation_(documents_, workspace_, settings_),
      shell_(workspace_, presentation_),
      theme_(themeFor(settings_.theme, settings_.accent)) {
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
    SetName("window.main");
#endif
    lifecycle_.recovery().setAppearanceProvider(
        [this](const core::DocumentId documentId)
            -> const metadata::TextAppearanceMap* {
            ensureTextAppearanceState(documentId);
            const auto iterator = appearanceStates_.find(documentId);
            if (iterator == appearanceStates_.end() || !iterator->second.loaded) {
                return nullptr;
            }
            return &iterator->second.appearance;
        });
#ifdef __WXMSW__
    configureNativeCustomFrame(static_cast<HWND>(GetHWND()), theme_);
#elif defined(__WXGTK__)
    configureGtkCustomFrame(*this);
    applyGtkThemeCss(theme_);
#endif
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(theme_.frame);
    searchSession_.options.caseSensitive = false;
    SetMinClientSize(wxSize(640, 400));
    CentreOnScreen();

    applyLayoutDensity();

    createChrome();
    bindCommands();
    installAccelerators();
    setupBranding();
    SetDropTarget(new WxFileDropTarget(
        [this](const std::vector<std::filesystem::path>& paths) {
            return openDroppedFiles(paths);
        }));

    app::LaunchRouteOptions routeOptions;
    routeOptions.inspect = settings_.inspectOptions;
    startupLaunch_ = app::LaunchRouter::route(startupRequest_, std::cin, routeOptions);
    initializeWorkspace();
    updateViewport();

    hostFactory_ = std::make_unique<WxEditorHostFactory>(
        *this,
        theme_,
        settings_,
        [this]() { scheduleSynchronize(); },
        [this](const workspace::ViewId view) {
            if (shell_.activateView(view)) {
                scheduleSynchronize();
            }
        },
        [this](wxWindow& anchor, const workspace::ViewId view) {
            showEditorContextMenu(anchor, view);
        },
        [this](const workspace::ViewId view, const core::LinkSpan& link) {
            activateDetectedLink(view, link);
        },
        [this](const workspace::ViewId view,
               const std::vector<std::filesystem::path>& paths) {
            return openDroppedFiles(paths, workspace_.paneContaining(view));
        });
    runtime_ = std::make_unique<GuiRuntime>(
        documents_,
        workspace_,
        presentation_,
        shell_,
        *hostFactory_,
        GuiRuntimePolicy{},
        GuiRuntimeCallbacks{
            .documentWillEdit = [this](const core::DocumentId document) {
                ensureTextAppearanceState(document);
            },
            .documentEdited = [this](const core::DocumentId document, const EditorTextEdit& edit) {
                noteTextAppearanceEdit(document, edit);
                lifecycle_.noteEdited(document);
            },
            .appearance = [this](const core::DocumentId document) {
                return appearanceForDocument(document);
            },
            .appearanceRangeRestored =
                [this](const core::DocumentId document,
                       const std::uint64_t begin,
                       const std::uint64_t end,
                       const std::span<const metadata::TextAppearanceSpan> spans) {
                    return restoreTextAppearanceRange(document, begin, end, spans);
                }});

    if (!settings_.sidebarRootUtf8.empty()) {
        const auto restoredRoot = pathFromUtf8(settings_.sidebarRootUtf8).lexically_normal();
        sidebarRoot_ = restoredRoot;
        sidebarScope_ = sidebarRoot_;
        presentation_.setSidebarFolderActive(true);
        sidebarIndexAvailable_ = false;
        if (settings_.sidebarVisible) {
            std::error_code rootError;
            sidebarRootUnavailable_ = !std::filesystem::is_directory(restoredRoot, rootError) ||
                                      static_cast<bool>(rootError);
            resetSidebarTree();
            updateSidebarHint();
        }
    }

    maintenanceTimer_.SetOwner(this, UiMaintenanceTimer);
    sidebarSearchTimer_.SetOwner(this, UiSidebarSearchTimer);
    dragPreviewTimer_.SetOwner(this, UiDragPreviewTimer);
    Bind(wxEVT_SIZE, &WxMainFrame::onSize, this);
    Bind(wxEVT_PAINT, &WxMainFrame::onFramePaint, this);
    Bind(wxEVT_CLOSE_WINDOW, &WxMainFrame::onClose, this);
    Bind(wxEVT_ACTIVATE, &WxMainFrame::onActivate, this);
    Bind(wxEVT_TIMER, &WxMainFrame::onMaintenanceTimer, this, UiMaintenanceTimer);
    Bind(wxEVT_TIMER, &WxMainFrame::onSidebarSearchTimer, this, UiSidebarSearchTimer);
    Bind(wxEVT_TIMER, &WxMainFrame::onDragPreviewTimer, this, UiDragPreviewTimer);
    Bind(wxEVT_MOTION, &WxMainFrame::onTabMotion, this);
    Bind(wxEVT_LEFT_UP, &WxMainFrame::onTabLeftUp, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &WxMainFrame::onTabCaptureLost, this);
    maintenanceTimer_.Start(500);
    applyTheme(settings_.theme);
    scheduleSynchronize();
    if (!pendingStartupPositions_.empty() || !startupLaunchIssues_.empty()) {
        CallAfter([this]() {
            if (shuttingDown_) {
                return;
            }
            applyStartupPositions();
            if (!startupLaunchIssues_.empty()) {
                std::string message;
                for (const auto& issue : startupLaunchIssues_) {
                    if (!message.empty()) {
                        message += '\n';
                    }
                    message += issue;
                }
                wxMessageBox(fromUtf8(message), "Startup inputs",
                             wxOK | wxICON_WARNING, this);
                startupLaunchIssues_.clear();
            }
        });
    }
    if (!unclaimedRecoverySnapshots_.empty()) {
        CallAfter([this]() {
            if (!shuttingDown_) {
                showStartupRecoveryPrompt();
            }
        });
    }
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
    automationFrameRef_ = std::make_shared<std::atomic<WxMainFrame*>>(this);
    auto frameRef = automationFrameRef_;
    automationPipe_ = std::make_unique<automation::windows::AutomationPipeServer>(
        static_cast<std::uint32_t>(::GetCurrentProcessId()),
        [frameRef](const std::string_view request) {
            auto* frame = frameRef->load(std::memory_order_acquire);
            if (frame == nullptr) {
                return std::string("ERROR\tshutting_down");
            }
            return frame->handleAutomationBridgeRequest(request);
        });
    if (!automationPipe_->start()) {
        automationPipe_.reset();
    }
#endif
}

WxMainFrame::~WxMainFrame() {
    shuttingDown_ = true;
    lifecycle_.recovery().setAppearanceProvider({});
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
    if (automationFrameRef_) {
        automationFrameRef_->store(nullptr, std::memory_order_release);
    }
    if (automationPipe_) {
        automationPipe_->stop();
        automationPipe_.reset();
    }
#endif
    cancelTabDrag();
    cancelWorkspaceSplitterDrag();
    maintenanceTimer_.Stop();
    sidebarSearchTimer_.Stop();
    dragPreviewTimer_.Stop();
    stopSidebarSearch();
    stopSidebarIndex();
    Unbind(wxEVT_SIZE, &WxMainFrame::onSize, this);
    Unbind(wxEVT_PAINT, &WxMainFrame::onFramePaint, this);
    Unbind(wxEVT_CLOSE_WINDOW, &WxMainFrame::onClose, this);
    Unbind(wxEVT_ACTIVATE, &WxMainFrame::onActivate, this);
    Unbind(wxEVT_TIMER, &WxMainFrame::onMaintenanceTimer, this, UiMaintenanceTimer);
    Unbind(wxEVT_TIMER, &WxMainFrame::onSidebarSearchTimer, this, UiSidebarSearchTimer);
    Unbind(wxEVT_TIMER, &WxMainFrame::onDragPreviewTimer, this, UiDragPreviewTimer);
    trayIcon_.reset();
    runtime_.reset();
    hostFactory_.reset();
}

#ifdef __WXMSW__
WXLRESULT WxMainFrame::MSWWindowProc(const WXUINT message,
                                     const WXWPARAM wParam,
                                     const WXLPARAM lParam) {
    const auto hwnd = static_cast<HWND>(GetHWND());
    const auto screenPoint = [lParam]() {
        const auto packed = static_cast<LPARAM>(lParam);
        const POINTS point = MAKEPOINTS(packed);
        return wxPoint(static_cast<int>(point.x), static_cast<int>(point.y));
    };

    switch (message) {
    case WM_ERASEBKGND: {

        const auto dc = reinterpret_cast<HDC>(wParam);
        if (hwnd != nullptr && dc != nullptr) {
            RECT client{};
            if (::GetClientRect(hwnd, &client) != FALSE) {
                const auto brush = ::CreateSolidBrush(RGB(theme_.frame.Red(),
                                                          theme_.frame.Green(),
                                                          theme_.frame.Blue()));
                if (brush != nullptr) {
                    static_cast<void>(::FillRect(dc, &client, brush));
                    static_cast<void>(::DeleteObject(brush));
                }
            }
        }
        return 1;
    }

    case WM_EXITSIZEMOVE:
        applyNativeFrameAppearance(hwnd, theme_);
        if (hwnd != nullptr) {
            static_cast<void>(::RedrawWindow(
                hwnd, nullptr, nullptr,
                RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN));
        }
        break;

    case WM_NCACTIVATE:

        applyNativeFrameAppearance(hwnd, theme_);
        if (hwnd != nullptr) {
            static_cast<void>(::RedrawWindow(
                hwnd, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW));
        }
        return TRUE;

    case WM_NCPAINT:

        applyNativeFrameAppearance(hwnd, theme_);
        return 0;

    case WM_DWMCOMPOSITIONCHANGED:
    case WM_THEMECHANGED:

        applyNativeFrameAppearance(hwnd, theme_);
        Refresh(false);
        break;

    case WM_DPICHANGED:

        applyNativeFrameAppearance(hwnd, theme_);
        Refresh(false);
        break;

    // windows moment
    case WM_NCCALCSIZE:
        if (wParam != FALSE) {

            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            if (params != nullptr) {
                insetMaximizedClientRect(hwnd, params->rgrc[0]);
            }
            return 0;
        }
        break;

    case WM_NCHITTEST: {
        const auto point = screenPoint();
        const POINT nativePoint{point.x, point.y};
        const auto resizeHit = resizeHitTest(hwnd, nativePoint, nativeResizeBorder(hwnd, true));
        if (resizeHit != HTNOWHERE) {
            return resizeHit;
        }

        if (titleBar_ != nullptr && titleBar_->IsShown()) {
            switch (titleBar_->hitTestScreen(point)) {
            case WxTitleBar::HitTarget::Drag:
                return HTCAPTION;
            case WxTitleBar::HitTarget::Minimize:
                return HTMINBUTTON;
            case WxTitleBar::HitTarget::Maximize:
                return HTMAXBUTTON;
            case WxTitleBar::HitTarget::Close:
                return HTCLOSE;
            case WxTitleBar::HitTarget::None:
                break;
            }
        }
        return HTCLIENT;
    }

    case WM_NCMOUSEMOVE:
        if (titleBar_ != nullptr && titleBar_->pressedTarget() == WxTitleBar::HitTarget::None) {
            const auto target = titleTargetFromNativeHit(wParam);
            titleBar_->setHotTarget(target == WxTitleBar::HitTarget::Drag
                                        ? WxTitleBar::HitTarget::None
                                        : target);

            TRACKMOUSEEVENT tracking{};
            tracking.cbSize = sizeof(tracking);
            tracking.dwFlags = TME_LEAVE | TME_NONCLIENT;
            tracking.hwndTrack = hwnd;
            static_cast<void>(::TrackMouseEvent(&tracking));
        }
        break;

    case WM_NCMOUSELEAVE:
        if (titleBar_ != nullptr && titleBar_->pressedTarget() == WxTitleBar::HitTarget::None) {
            titleBar_->setHotTarget(WxTitleBar::HitTarget::None);
        }
        return 0;

    case WM_NCLBUTTONDOWN:
        if (titleBar_ != nullptr) {
            const auto target = titleTargetFromNativeHit(wParam);
            if (target == WxTitleBar::HitTarget::Minimize ||
                target == WxTitleBar::HitTarget::Maximize ||
                target == WxTitleBar::HitTarget::Close) {
                titleBar_->setHotTarget(target);
                titleBar_->setPressedTarget(target);
                if (hwnd != nullptr) {
                    ::SetCapture(hwnd);
                }
                return 0;
            }
            if (target == WxTitleBar::HitTarget::Drag) {
                titleBar_->clearInteraction();
            }
        }
        break;

    case WM_MOUSEMOVE:
        if (titleBar_ != nullptr &&
            titleBar_->pressedTarget() != WxTitleBar::HitTarget::None) {
            const auto target = titleBar_->hitTestScreen(
                clientMessageScreenPoint(hwnd, static_cast<LPARAM>(lParam)));
            titleBar_->setHotTarget(target == titleBar_->pressedTarget()
                                        ? target
                                        : WxTitleBar::HitTarget::None);
            return 0;
        }
        break;

    case WM_LBUTTONUP:
        if (titleBar_ != nullptr &&
            titleBar_->pressedTarget() != WxTitleBar::HitTarget::None) {
            const auto target = titleBar_->hitTestScreen(
                clientMessageScreenPoint(hwnd, static_cast<LPARAM>(lParam)));
            const auto pressed = titleBar_->pressedTarget();
            titleBar_->setPressedTarget(WxTitleBar::HitTarget::None);
            titleBar_->setHotTarget(target == pressed ? target : WxTitleBar::HitTarget::None);
            if (hwnd != nullptr && ::GetCapture() == hwnd) {
                static_cast<void>(::ReleaseCapture());
            }
            if (target == pressed) {
                switch (target) {
                case WxTitleBar::HitTarget::Minimize:
                    Iconize(true);
                    break;
                case WxTitleBar::HitTarget::Maximize:
                    Maximize(!IsMaximized());
                    titleBar_->Refresh(false);
                    break;
                case WxTitleBar::HitTarget::Close:
                    Close();
                    break;
                case WxTitleBar::HitTarget::None:
                case WxTitleBar::HitTarget::Drag:
                    break;
                }
            }
            return 0;
        }
        break;

    case WM_NCLBUTTONUP:

        if (titleBar_ != nullptr &&
            titleBar_->pressedTarget() != WxTitleBar::HitTarget::None) {
            const auto target = titleTargetFromNativeHit(wParam);
            const auto pressed = titleBar_->pressedTarget();
            titleBar_->setPressedTarget(WxTitleBar::HitTarget::None);
            titleBar_->setHotTarget(target == pressed ? target : WxTitleBar::HitTarget::None);
            if (hwnd != nullptr && ::GetCapture() == hwnd) {
                static_cast<void>(::ReleaseCapture());
            }
            if (target == pressed) {
                switch (target) {
                case WxTitleBar::HitTarget::Minimize:
                    Iconize(true);
                    break;
                case WxTitleBar::HitTarget::Maximize:
                    Maximize(!IsMaximized());
                    titleBar_->Refresh(false);
                    break;
                case WxTitleBar::HitTarget::Close:
                    Close();
                    break;
                case WxTitleBar::HitTarget::None:
                case WxTitleBar::HitTarget::Drag:
                    break;
                }
            }
            return 0;
        }
        break;

    case WM_CAPTURECHANGED:
        if (titleBar_ != nullptr) {
            titleBar_->setPressedTarget(WxTitleBar::HitTarget::None);
            titleBar_->setHotTarget(WxTitleBar::HitTarget::None);
        }
        break;

    default:
        break;
    }

    return wxFrame::MSWWindowProc(message, wParam, lParam);
}
#endif

void WxMainFrame::onSize(wxSizeEvent& event) {
    updateOuterFrameInset();
    if (titleBar_ != nullptr) {
        titleBar_->Refresh(false);
    }
    updateViewport();
    updateResizeZones();
    Refresh(false);
    scheduleSynchronize();
    event.Skip();
}

void WxMainFrame::onFramePaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(theme_.frame));
    dc.DrawRectangle(GetClientRect());
}

void WxMainFrame::onClose(wxCloseEvent& event) {
    std::size_t modified = 0U;
    for (const auto id : documents_.ids()) {
        const auto* document = documents_.get(id);
        if (document != nullptr && document->modified()) {
            ++modified;
        }
    }

    if (modified != 0U && event.CanVeto()) {
        const auto message = wxString::Format(
            "%llu documents have unsaved changes. Exit notepadFasaFiso?",
            static_cast<unsigned long long>(modified));
        if (showThemedConfirmation(this, theme_, "Unsaved changes", message,
                                   "Exit", true) != wxID_YES) {
            event.Veto();
            return;
        }
    }
    const auto saved = persistApplicationState();
    if (!saved && event.CanVeto()) {
        wxString details = "notepadFasaFiso could not preserve recovery/session state.";
        if (!saved.issues.empty() && saved.issues.front().error) {
            details += "\n\n";
            details += fromUtf8(saved.issues.front().error.message());
        }
        details += "\n\nExit anyway? Unsaved changes may be lost.";
        if (showThemedConfirmation(this, theme_, "State save failed", details,
                                   "Exit anyway", true) != wxID_YES) {
            event.Veto();
            return;
        }
    }
    event.Skip();
}

void WxMainFrame::onActivate(wxActivateEvent& event) {
    if (titleBar_ != nullptr) {
        titleBar_->setActive(event.GetActive());
    }
    if (!event.GetActive()) {
        if (const auto document = activeDocumentId()) {
            const auto result = lifecycle_.focusLost(*document);
            if (result.outcome == recovery::AutoSaveOutcome::RecoveryCheckpoint ||
                result.outcome == recovery::AutoSaveOutcome::FileSaved) {
                static_cast<void>(persistTextAppearance(*document));
            }
            if (result.outcome == recovery::AutoSaveOutcome::Failed && result.error &&
                result.error != lastMaintenanceError_) {
                lastMaintenanceError_ = result.error;
                showError("Recovery failed", result.error);
            }
        }
    }
    event.Skip();
}

void WxMainFrame::onMaintenanceTimer(wxTimerEvent& event) {
    processSidebarIndexCompletion();
    handleMaintenance(lifecycle_.poll());
    if (runtime_ != nullptr) {
        const auto live = runtime_->pollLiveContent();
        if (live.changed) {
            scheduleSynchronize();
        }
    }
    event.Skip();
}

void WxMainFrame::onSidebarQuery(wxCommandEvent& event) {
    if (sidebarSearch_ != nullptr) {
        sidebarSearchQuery_ = toUtf8(sidebarSearch_->GetValue());

        requestSidebarSearch(sidebarSearchQuery_);
        sidebarGenerationCache_ = fileSearch_.generation();
        sidebarQueryCache_ = sidebarSearchQuery_;
        sidebarScopeCache_ = pathUtf8(sidebarSearchScope());
    }
    event.Skip();
}

void WxMainFrame::onSidebarSearchTimer(wxTimerEvent& event) {
    processSidebarSearchCompletion();
    event.Skip();
}

void WxMainFrame::onDragPreviewTimer(wxTimerEvent& event) {
    if (workspaceSplitDrag_) {
        updateWorkspaceSplitterDragFromPointer();
    }
    if (sidebarDragging_) {
        updateSidebarSplitterDragFromPointer();
    }
    updateDragPreviewTimerState();
    event.Skip(false);
}

void WxMainFrame::onSidebarTreeActivated(wxTreeEvent& event) {
    if (sidebarTree_ == nullptr) {
        event.Skip();
        return;
    }

    const auto item = event.GetItem();
    auto* data = sidebarTreeData(sidebarTree_, item);
    if (data == nullptr) {
        event.Skip();
        return;
    }

    if (data->directory) {
        if (!sidebarTree_->IsExpanded(item)) {
            sidebarTree_->Expand(item);
        }
        return;
    }

    static_cast<void>(openPath(data->path));
}

void WxMainFrame::onSidebarTreeExpanding(wxTreeEvent& event) {
    if (sidebarTree_ == nullptr) {
        event.Skip();
        return;
    }
    auto* data = sidebarTreeData(sidebarTree_, event.GetItem());
    if (data != nullptr && data->directory && !data->loaded && !sidebarSearchTreeMode_) {
        populateSidebarDirectory(event.GetItem(), data->path);
    }
    event.Skip();
}

void WxMainFrame::onSidebarTreeSelectionChanged(wxTreeEvent& event) {
    event.Skip();
}

void WxMainFrame::onSidebarTreeLeftDown(wxMouseEvent& event) {
    sidebarTreeDragPath_.reset();
    sidebarTreeDragActive_ = false;
    if (sidebarTree_ != nullptr) {
        int flags = 0;
        const auto item = sidebarTree_->HitTest(event.GetPosition(), flags);
        const auto* data = sidebarTreeData(sidebarTree_, item);
        if (data != nullptr && !data->directory && !data->path.empty()) {
            sidebarTreeDragPath_ = data->path;
            const auto screen = sidebarTree_->ClientToScreen(event.GetPosition());
            sidebarTreeDragStartScreenX_ = screen.x;
            sidebarTreeDragStartScreenY_ = screen.y;
        }
    }
    event.Skip();
}

void WxMainFrame::onSidebarTreeMotion(wxMouseEvent& event) {
    if (!sidebarTreeDragPath_ || sidebarTree_ == nullptr || !event.LeftIsDown()) {
        event.Skip();
        return;
    }

    const auto screen = sidebarTree_->ClientToScreen(event.GetPosition());
    if (!sidebarTreeDragActive_) {
        constexpr int dragThreshold = 6;
        const auto distance = std::abs(screen.x - sidebarTreeDragStartScreenX_) +
                              std::abs(screen.y - sidebarTreeDragStartScreenY_);
        if (distance < dragThreshold) {
            event.Skip();
            return;
        }
        sidebarTreeDragActive_ = true;
        cancelTabDrag();
        cancelWorkspaceSplitterDrag();
        if (!sidebarTree_->HasCapture()) {
            sidebarTree_->CaptureMouse();
        }
        SetCursor(wxCursor(wxCURSOR_HAND));
    }

    updateSidebarTreeDropIndicator(screen.x, screen.y);
    event.Skip(false);
}

void WxMainFrame::onSidebarTreeLeftUp(wxMouseEvent& event) {
    if (!sidebarTreeDragPath_ || sidebarTree_ == nullptr) {
        event.Skip();
        return;
    }

    const auto path = *sidebarTreeDragPath_;
    const auto dragged = sidebarTreeDragActive_;
    const auto screen = sidebarTree_->ClientToScreen(event.GetPosition());
    if (!dragged) {
        sidebarTreeDragPath_.reset();
        sidebarTreeDragActive_ = false;
        event.Skip();
        return;
    }

    cancelSidebarTreeDrag();
    finishSidebarTreeDrag(path, screen.x, screen.y);
    event.Skip(false);
}

void WxMainFrame::onSidebarTreeCaptureLost(wxMouseCaptureLostEvent& event) {
    cancelSidebarTreeDrag();
    event.Skip();
}

void WxMainFrame::onSidebarSearchKeyDown(wxKeyEvent& event) {
    if (sidebarTree_ == nullptr) {
        event.Skip();
        return;
    }

    const auto key = event.GetKeyCode();
    if (key == WXK_RETURN || key == WXK_NUMPAD_ENTER) {
        static_cast<void>(openSidebarSelection());
        return;
    }
    if (key != WXK_DOWN && key != WXK_UP) {
        event.Skip();
        return;
    }

    auto selection = sidebarTree_->GetSelection();
    if (!selection.IsOk()) {
        selection = sidebarTree_->GetRootItem();
    }
    const auto next = key == WXK_DOWN ? sidebarTree_->GetNextVisible(selection)
                                      : sidebarTree_->GetPrevVisible(selection);
    if (next.IsOk()) {
        sidebarTree_->SelectItem(next);
        sidebarTree_->EnsureVisible(next);
    }
}

void WxMainFrame::onSidebarSplitterDown(wxMouseEvent& event) {
    cancelTabDrag();
    sidebarDragging_ = true;
    sidebarSplitDragWidth_.reset();
    updateDragPreviewTimerState();
    if (sidebarSplitter_ != nullptr) {
        if (auto* handle = dynamic_cast<WxSplitterHandle*>(sidebarSplitter_)) {
            handle->setActive(true);
        }
        if (!sidebarSplitter_->HasCapture()) {
            sidebarSplitter_->CaptureMouse();
        }
    }
    event.Skip(false);
}

void WxMainFrame::onSidebarSplitterMotion(wxMouseEvent& event) {
    if (sidebarDragging_ && event.Dragging() && event.LeftIsDown()) {
        updateSidebarSplitterDragFromPointer();
    }
    event.Skip(false);
}

void WxMainFrame::onSidebarSplitterUp(wxMouseEvent& event) {
    const auto width = sidebarSplitDragWidth_;
    cancelSidebarSplitterDrag();

    CallAfter([this, width]() {
        if (shuttingDown_ || !width) {
            return;
        }
        shell_.setSidebarWidth(*width);
        synchronizeNow();
    });
    event.Skip(false);
}

void WxMainFrame::onWorkspaceSplitterDown(wxMouseEvent& event,
                                           const workspace::SplitId split) {
    auto iterator = std::ranges::find_if(
        shell_.frame().layout.splitters,
        [split](const SplitterLayout& layout) { return layout.split == split; });
    if (iterator == shell_.frame().layout.splitters.end()) {
        event.Skip();
        return;
    }

    auto* source = static_cast<wxWindow*>(event.GetEventObject());
    const auto screen = source != nullptr ? source->ClientToScreen(event.GetPosition())
                                          : wxGetMousePosition();
    const auto client = ScreenToClient(screen);
    const auto originalOrientation = iterator->orientation;

    const bool pivoted =
        originalOrientation == workspace::SplitOrientation::Vertical
            ? shell_.normalizeAlignedTwoByTwoColumns()
            : shell_.normalizeAlignedTwoByTwoRows();

    workspace::SplitId dragSplit = split;
    if (pivoted) {
        static_cast<void>(shell_.refresh());
        const auto hit = shell_.hitTest(
            Point{static_cast<double>(client.x), static_cast<double>(client.y)});
        if (hit.region == HitRegion::WorkspaceSplitter && hit.split) {
            dragSplit = hit.split;
        }
    }

    iterator = std::ranges::find_if(
        shell_.frame().layout.splitters,
        [dragSplit](const SplitterLayout& layout) { return layout.split == dragSplit; });
    if (iterator == shell_.frame().layout.splitters.end()) {
        event.Skip();
        return;
    }

    cancelTabDrag();
    workspaceSplitDrag_ = dragSplit;
    workspaceSplitDragRatio_.reset();
    for (auto& widget : workspaceSplitterWidgets_) {
        if (auto* handle = dynamic_cast<WxSplitterHandle*>(widget.panel)) {
            handle->setActive(widget.split == dragSplit);
        }
    }
    updateDragPreviewTimerState();
    SetCursor(wxCursor(iterator->orientation == workspace::SplitOrientation::Horizontal
                           ? wxCURSOR_SIZEWE
                           : wxCURSOR_SIZENS));
    if (!HasCapture()) {
        CaptureMouse();
    }
    event.Skip(false);
}

void WxMainFrame::onTabLeftDown(wxMouseEvent& event,
                                const workspace::ViewId view,
                                const workspace::PaneId pane) {
    cancelTabDrag();
    auto* source = static_cast<wxWindow*>(event.GetEventObject());
    if (source == nullptr) {
        return;
    }

    const auto screen = source->ClientToScreen(event.GetPosition());
    tabDragView_ = view;
    tabDragSourcePane_ = pane;
    tabDragStartScreenX_ = screen.x;
    tabDragStartScreenY_ = screen.y;
    tabDragActive_ = false;

    if (!HasCapture()) {
        CaptureMouse();
    }
    event.Skip(false);
}

void WxMainFrame::updateWorkspaceSplitterDragFromPointer() {
    if (!workspaceSplitDrag_) {
        return;
    }
    const auto split = *workspaceSplitDrag_;
    const auto iterator = std::ranges::find_if(
        shell_.frame().layout.splitters,
        [split](const SplitterLayout& layout) { return layout.split == split; });
    if (iterator == shell_.frame().layout.splitters.end()) {
        return;
    }

    const auto point = ScreenToClient(wxGetMousePosition());
    const auto& track = iterator->track;
    const auto metrics = shell_.layoutMetrics();
    const bool horizontal = iterator->orientation == workspace::SplitOrientation::Horizontal;
    const double span = horizontal
                            ? std::max(0.0, track.width - metrics.splitterThickness)
                            : std::max(0.0, track.height - metrics.splitterThickness);
    if (span <= 0.0) {
        return;
    }

    const double position = horizontal
                                ? static_cast<double>(point.x) - track.x
                                : static_cast<double>(point.y) - track.y;
    const double minimum = horizontal ? metrics.minimumPaneWidth : metrics.minimumPaneHeight;
    const double minimumRatio = std::min(0.49, minimum / span);
    double ratio = std::clamp(position / span, minimumRatio, 1.0 - minimumRatio);
    if (const auto snapped = ShellLayoutEngine::snappedSplitterRatio(
            *iterator, ratio, shell_.frame().layout.splitters, metrics, 10.0)) {
        ratio = *snapped;
    }

    workspaceSplitDragRatio_ = ratio;
    updateWorkspaceSplitterPreview(*iterator, ratio);
}

void WxMainFrame::updateSidebarSplitterDragFromPointer() {
    if (!sidebarDragging_) {
        return;
    }
    const auto client = ScreenToClient(wxGetMousePosition());
    const auto& layout = shell_.frame().layout;
    const auto metrics = shell_.layoutMetrics();
    const double maximumByViewport = std::max(
        0.0,
        layout.contentFrame.width - metrics.minimumPaneWidth - metrics.splitterThickness);
    const double maximum = std::min(app::PresentationModel::maximumSidebarWidth,
                                    maximumByViewport);
    const double minimum = std::min(app::PresentationModel::minimumSidebarWidth, maximum);
    const double requested = static_cast<double>(client.x) - layout.contentFrame.x;
    const double width = std::clamp(requested, minimum, maximum);
    sidebarSplitDragWidth_ = width;
    updateSidebarSplitterPreview(width);
}

void WxMainFrame::updateDragPreviewTimerState() noexcept {
    const bool active = workspaceSplitDrag_.has_value() || sidebarDragging_;
    if (active) {
        if (!dragPreviewTimer_.IsRunning()) {
            dragPreviewTimer_.Start(16);
        }
    } else if (dragPreviewTimer_.IsRunning()) {
        dragPreviewTimer_.Stop();
    }
}

void WxMainFrame::onTabMotion(wxMouseEvent& event) {
    if (workspaceSplitDrag_ && HasCapture() && event.Dragging() && event.LeftIsDown()) {
        updateWorkspaceSplitterDragFromPointer();
        event.Skip(false);
        return;
    }

    if (!tabDragView_ || !HasCapture() || !event.Dragging() || !event.LeftIsDown()) {
        event.Skip();
        return;
    }

    const auto screen = ClientToScreen(event.GetPosition());
    if (!tabDragActive_) {
        constexpr int dragThreshold = 6;
        const auto distance = std::abs(screen.x - tabDragStartScreenX_) +
                              std::abs(screen.y - tabDragStartScreenY_);
        tabDragActive_ = distance >= dragThreshold;
    }

    if (tabDragActive_) {
        SetCursor(wxCursor(wxCURSOR_HAND));
        updateDraggedTabVisual();
        updateTabDropIndicator(screen.x, screen.y);
    }
    event.Skip(false);
}

void WxMainFrame::onTabLeftUp(wxMouseEvent& event) {
    if (workspaceSplitDrag_) {
        const auto split = *workspaceSplitDrag_;
        const auto ratio = workspaceSplitDragRatio_;
        cancelWorkspaceSplitterDrag();

        CallAfter([this, split, ratio]() {
            if (shuttingDown_) {
                return;
            }
            if (ratio && shell_.setSplitRatio(split, *ratio)) {
                synchronizeNow();
            }
            static_cast<void>(lifecycle_.saveSession());
        });
        event.Skip(false);
        return;
    }

    if (!tabDragView_) {
        event.Skip();
        return;
    }

    const auto view = *tabDragView_;
    const auto sourcePane = tabDragSourcePane_;
    const auto screen = ClientToScreen(event.GetPosition());
    const bool dragged = tabDragActive_;

    tabDragView_.reset();
    tabDragSourcePane_ = {};
    tabDragActive_ = false;
    hideTabDropIndicator();
    updateDraggedTabVisual();
    SetCursor(wxNullCursor);
    if (HasCapture()) {
        ReleaseMouse();
    }

    CallAfter([this, view, sourcePane, screenX = screen.x, screenY = screen.y, dragged]() {
        if (shuttingDown_) {
            return;
        }
        if (!dragged) {
            if (shell_.activateView(view)) {
                static_cast<void>(lifecycle_.saveSession());
                scheduleSynchronize();
            }
            return;
        }
        finishTabDrag(view, sourcePane, screenX, screenY);
    });
    event.Skip(false);
}

void WxMainFrame::onTabCaptureLost(wxMouseCaptureLostEvent& event) {
    tabDragView_.reset();
    tabDragSourcePane_ = {};
    tabDragActive_ = false;
    workspaceSplitDrag_.reset();
    workspaceSplitDragRatio_.reset();
    sidebarDragging_ = false;
    sidebarSplitDragWidth_.reset();
    updateDragPreviewTimerState();
    hideWorkspaceSplitterPreview();
    hideSidebarSplitterPreview();
    hideTabDropIndicator();
    updateDraggedTabVisual();
    if (auto* handle = dynamic_cast<WxSplitterHandle*>(sidebarSplitter_)) {
        handle->setActive(false);
    }
    for (auto& widget : workspaceSplitterWidgets_) {
        if (auto* handle = dynamic_cast<WxSplitterHandle*>(widget.panel)) {
            handle->setActive(false);
        }
    }
    SetCursor(wxNullCursor);

    event.Skip(false);
}

void WxMainFrame::finishTabDrag(const workspace::ViewId view,
                                const workspace::PaneId,
                                const int screenX,
                                const int screenY) {
    if (workspace_.view(view) == nullptr) {
        return;
    }

    const auto client = ScreenToClient(wxPoint(screenX, screenY));
    const auto hit = shell_.hitTest(
        Point{static_cast<double>(client.x), static_cast<double>(client.y)});
    if (hit.region != HitRegion::PaneTabStrip && hit.region != HitRegion::PaneContent) {
        return;
    }

    std::optional<std::size_t> targetIndex;
    if (hit.region == HitRegion::PaneTabStrip) {
        targetIndex = tabDropIndex(hit.pane, view, screenX);
    }

    if (shell_.moveView(view, hit.pane, targetIndex)) {
        static_cast<void>(lifecycle_.saveSession());
        tabsFingerprint_.reset();
        scheduleSynchronize();
    }
}

void WxMainFrame::cancelSidebarTreeDrag() noexcept {
    sidebarTreeDragPath_.reset();
    sidebarTreeDragActive_ = false;
    hideTabDropIndicator();
    SetCursor(wxNullCursor);
    if (sidebarTree_ != nullptr && sidebarTree_->HasCapture()) {
        sidebarTree_->ReleaseMouse();
    }
}

void WxMainFrame::updateSidebarTreeDropIndicator(const int screenX, const int screenY) {
    const auto client = ScreenToClient(wxPoint(screenX, screenY));
    const auto hit = shell_.hitTest(
        Point{static_cast<double>(client.x), static_cast<double>(client.y)});
    if (hit.region != HitRegion::PaneTabStrip && hit.region != HitRegion::PaneContent) {
        hideTabDropIndicator();
        return;
    }

    for (auto& tab : tabWidgets_) {
        if (auto* polished = dynamic_cast<WxPolishedTab*>(tab.button)) {
            polished->setDropCue(tab.pane == hit.pane ? TabDropCue::Pane : TabDropCue::None);
        }
    }
}

void WxMainFrame::finishSidebarTreeDrag(const std::filesystem::path& path,
                                        const int screenX,
                                        const int screenY) {
    const auto client = ScreenToClient(wxPoint(screenX, screenY));
    const auto hit = shell_.hitTest(
        Point{static_cast<double>(client.x), static_cast<double>(client.y)});
    if (hit.region != HitRegion::PaneTabStrip && hit.region != HitRegion::PaneContent) {
        return;
    }
    static_cast<void>(openPathInPane(path, hit.pane));
}

void WxMainFrame::cancelTabDrag() noexcept {
    tabDragView_.reset();
    tabDragSourcePane_ = {};
    tabDragActive_ = false;
    hideTabDropIndicator();
    updateDraggedTabVisual();
    SetCursor(wxNullCursor);
    if (HasCapture() && !workspaceSplitDrag_) {
        ReleaseMouse();
    }
}

void WxMainFrame::cancelSidebarSplitterDrag() noexcept {
    sidebarDragging_ = false;
    sidebarSplitDragWidth_.reset();
    hideSidebarSplitterPreview();
    updateDragPreviewTimerState();
    if (sidebarSplitter_ != nullptr) {
        if (sidebarSplitter_->HasCapture()) {
            sidebarSplitter_->ReleaseMouse();
        }
        if (auto* handle = dynamic_cast<WxSplitterHandle*>(sidebarSplitter_)) {
            handle->setActive(false);
        }
    }
}

void WxMainFrame::cancelWorkspaceSplitterDrag() noexcept {
    hideWorkspaceSplitterPreview();
    workspaceSplitDrag_.reset();
    workspaceSplitDragRatio_.reset();
    for (auto& widget : workspaceSplitterWidgets_) {
        if (auto* handle = dynamic_cast<WxSplitterHandle*>(widget.panel)) {
            handle->setActive(false);
        }
    }
    updateDragPreviewTimerState();
    SetCursor(wxNullCursor);
    if (HasCapture()) {
        ReleaseMouse();
    }
}

void WxMainFrame::hidePaneDropIndicator() noexcept {
    for (auto* edge : tabDropPanePreview_) {
        if (edge != nullptr && edge->IsShown()) {
            edge->Hide();
        }
    }
}

void WxMainFrame::showPaneDropIndicator(const workspace::PaneId pane) {
    const auto found = std::ranges::find_if(
        shell_.frame().layout.panes,
        [pane](const PaneLayout& layout) { return layout.pane == pane; });
    if (found == shell_.frame().layout.panes.end() || found->bounds.empty()) {
        hidePaneDropIndicator();
        return;
    }

    constexpr double borderThickness = 2.0;
    constexpr double borderInset = 3.0;
    const auto border = paneDropBorder(found->bounds, borderThickness, borderInset);
    const std::array edges{border.top, border.right, border.bottom, border.left};
    for (std::size_t index = 0U; index < edges.size(); ++index) {
        if (tabDropPanePreview_[index] == nullptr) {
            tabDropPanePreview_[index] = new wxPopupWindow(this, wxBORDER_NONE);
        }
        auto* edge = tabDropPanePreview_[index];
        const auto& rect = edges[index];
        const auto screen = ClientToScreen(wxPoint(toPixel(rect.x), toPixel(rect.y)));
        edge->SetBackgroundColour(theme_.accent);
        edge->SetSize(screen.x, screen.y,
                      std::max(1, toPixel(rect.width)),
                      std::max(1, toPixel(rect.height)),
                      wxSIZE_FORCE);
        edge->Show(true);
        edge->Raise();
        edge->Refresh(false);
    }
}

void WxMainFrame::updateDraggedTabVisual() noexcept {
    for (auto& tab : tabWidgets_) {
        if (auto* polished = dynamic_cast<WxPolishedTab*>(tab.button)) {
            polished->setDragging(tabDragActive_ && tabDragView_ && tab.view == *tabDragView_);
        }
    }
}

void WxMainFrame::hideTabDropIndicator() noexcept {
    hidePaneDropIndicator();
    for (auto& tab : tabWidgets_) {
        if (auto* polished = dynamic_cast<WxPolishedTab*>(tab.button)) {
            polished->setDropCue(TabDropCue::None);
        }
    }
}

void WxMainFrame::updateTabDropIndicator(const int screenX, const int screenY) {
    if (!tabDragView_) {
        hideTabDropIndicator();
        return;
    }

    const auto client = ScreenToClient(wxPoint(screenX, screenY));
    const auto hit = shell_.hitTest(
        Point{static_cast<double>(client.x), static_cast<double>(client.y)});
    if (hit.region != HitRegion::PaneTabStrip && hit.region != HitRegion::PaneContent) {
        hideTabDropIndicator();
        return;
    }

    showPaneDropIndicator(hit.pane);
    updateDraggedTabVisual();

    std::vector<WxPolishedTab*> controls;
    controls.reserve(tabWidgets_.size());
    for (auto& tab : tabWidgets_) {
        if (tab.pane != hit.pane || tab.button == nullptr ||
            tab.view == *tabDragView_) {
            continue;
        }
        if (auto* polished = dynamic_cast<WxPolishedTab*>(tab.button)) {
            controls.push_back(polished);
        }
    }

    if (controls.empty()) {
        for (auto& tab : tabWidgets_) {
            if (auto* polished = dynamic_cast<WxPolishedTab*>(tab.button)) {
                polished->setDropCue(TabDropCue::None);
            }
        }
        return;
    }

    if (hit.region == HitRegion::PaneContent) {
        for (auto& tab : tabWidgets_) {
            auto* polished = dynamic_cast<WxPolishedTab*>(tab.button);
            if (polished != nullptr) {
                polished->setDropCue(tab.pane == hit.pane ? TabDropCue::Pane
                                                          : TabDropCue::None);
            }
        }
        return;
    }

    const auto target = tabDropIndex(hit.pane, *tabDragView_, screenX);
    if (!target) {
        hideTabDropIndicator();
        return;
    }

    for (auto& tab : tabWidgets_) {
        if (auto* polished = dynamic_cast<WxPolishedTab*>(tab.button)) {
            polished->setDropCue(TabDropCue::None);
        }
    }

    if (*target < controls.size()) {
        controls[*target]->setDropCue(TabDropCue::Before);
    } else {
        controls.back()->setDropCue(TabDropCue::After);
    }
}

std::optional<std::size_t> WxMainFrame::tabDropIndex(
    const workspace::PaneId pane,
    const workspace::ViewId movingView,
    const int screenX) const {
    std::size_t index = 0U;
    bool foundPane = false;
    for (const auto& tab : tabWidgets_) {
        if (tab.pane != pane || tab.button == nullptr) {
            continue;
        }
        foundPane = true;
        if (tab.view == movingView) {
            continue;
        }
        const auto origin = tab.button->ClientToScreen(wxPoint(0, 0));
        const auto width = tab.button->GetClientSize().GetWidth();
        const auto midpoint = origin.x + width / 2;
        if (screenX < midpoint) {
            return index;
        }
        ++index;
    }
    return foundPane ? std::optional<std::size_t>{index} : std::nullopt;
}

void WxMainFrame::scheduleSynchronize() {
    if (shuttingDown_ || synchronizePending_) {
        return;
    }
    synchronizePending_ = true;
    CallAfter([this]() {
        synchronizePending_ = false;
        if (!shuttingDown_) {
            synchronizeNow();
        }
    });
}

void WxMainFrame::synchronizeNow() {
    if (runtime_ == nullptr) {
        return;
    }
    updateViewport();
    const auto& frame = runtime_->synchronize();
    updateChrome(frame);
}

void WxMainFrame::updateViewport() noexcept {
    const auto client = GetClientSize();
    shell_.setViewport(Size{
        static_cast<double>(std::max(client.GetWidth(), 0)),
        static_cast<double>(std::max(client.GetHeight(), 0)),
    });
}

void WxMainFrame::createInitialDocument() {
    const auto document = documents_.createUntitled();
    if (!document) {
        return;
    }
    const auto pane = workspace_.primaryPane();
    const auto view = workspace_.openView(document, pane);
    if (!view) {
        static_cast<void>(documents_.close(document));
        return;
    }
    static_cast<void>(workspace_.setActiveView(pane, view));
    static_cast<void>(workspace_.setActivePane(pane));
    presentation_.sync();
    shell_.requestRefresh();
}

void WxMainFrame::initializeWorkspace() {
    auto startup = lifecycle_.start();
    for (auto& restored : startup.session.documents) {
        if (restored.source == session::RestoredDocumentSource::Recovery) {
            presentation_.setRecovered(restored.runtimeId, true);
            if (restored.recoveredAppearance.has_value()) {
                auto& state = appearanceStates_[restored.runtimeId];
                state.appearance = std::move(*restored.recoveredAppearance);
                state.loaded = true;
                ++state.revision;
            }
        }
    }
    unclaimedRecoverySnapshots_ = std::move(startup.unclaimedRecoverySnapshots);

    applyStartupLaunch();

    if (workspace_.viewCount() == 0U) {
        createInitialDocument();
    }

    presentation_.sync();
    shell_.requestRefresh();
}

void WxMainFrame::applyStartupLaunch() {
    for (const auto& target : startupLaunch_.targets()) {
        if (!target) {
            const auto label = target.backingPath.empty()
                                   ? std::string{"stdin"}
                                   : pathUtf8(target.backingPath);
            startupLaunchIssues_.push_back(label + ": " + target.error.message());
            continue;
        }

        const auto view = openStartupTarget(target);
        if (!view) {
            continue;
        }
        if (target.requested.position.hasLine) {
            pendingStartupPositions_.push_back({*view, target.requested.position});
        }
    }

    if (!startupLaunch_.targets().empty()) {
        static_cast<void>(lifecycle_.saveSession());
    }
}

std::optional<workspace::ViewId> WxMainFrame::openStartupTarget(
    const app::RoutedTarget& target) {
    core::OpenDocumentResult opened;
    if (target.inputKind == app::RoutedInputKind::NewFile) {
        opened = documents_.createNewFile(target.backingPath);
        if (!opened && opened.error == std::errc::file_exists) {
            opened = documents_.openRouted(target.backingPath, settings_.inspectOptions);
        }
    } else {
        if (!target.profile) {
            startupLaunchIssues_.push_back(
                pathUtf8(target.backingPath) + ": missing inspected file profile");
            return std::nullopt;
        }
        auto profile = *target.profile;
        profile.recommendedMode = target.openMode;

        std::size_t maximumBytes = core::Document::defaultEditorLoadLimit;
        if (target.openMode == core::OpenMode::Editor) {
            if (profile.fileSize > static_cast<std::uintmax_t>(
                    std::numeric_limits<std::size_t>::max())) {
                startupLaunchIssues_.push_back(
                    pathUtf8(target.backingPath) + ": file is too large to edit on this build");
                return std::nullopt;
            }
            maximumBytes = std::max(maximumBytes, static_cast<std::size_t>(profile.fileSize));
        }
        opened = documents_.openPrepared(target.backingPath, std::move(profile), maximumBytes);
    }

    if (!opened) {
        const auto label = target.backingPath.empty()
                               ? std::string{"stdin"}
                               : pathUtf8(target.backingPath);
        startupLaunchIssues_.push_back(label + ": " + opened.error.message());
        return std::nullopt;
    }

    auto* document = documents_.get(opened.id);
    if (document == nullptr) {
        startupLaunchIssues_.push_back("startup target lost its document state");
        return std::nullopt;
    }

    if (target.openMode == core::OpenMode::Editor && !document->textBufferLoaded()) {
        if (document->profile().fileSize > static_cast<std::uintmax_t>(
                std::numeric_limits<std::size_t>::max())) {
            startupLaunchIssues_.push_back(
                pathUtf8(target.backingPath) + ": file is too large to edit on this build");
            return std::nullopt;
        }
        const auto error = documents_.materializeForEdit(
            opened.id, static_cast<std::size_t>(document->profile().fileSize));
        if (error) {
            startupLaunchIssues_.push_back(
                pathUtf8(target.backingPath) + ": " + error.message());
            return std::nullopt;
        }
        document = documents_.get(opened.id);
    }

    std::optional<workspace::ViewId> view;
    workspace_.forEachView([&view, document = opened.id](const workspace::ViewState& existing) {
        if (!view && existing.document == document) view = existing.id;
    });

    if (!view) {
        const auto pane = workspace_.activePane() ? workspace_.activePane() : workspace_.primaryPane();
        const auto created = workspace_.openView(opened.id, pane);
        if (!created) {
            if (!opened.reusedExisting) {
                static_cast<void>(documents_.close(opened.id));
            }
            startupLaunchIssues_.push_back(
                pathUtf8(target.backingPath) + ": could not create a workspace view");
            return std::nullopt;
        }
        view = created;
        static_cast<void>(workspace_.setActiveView(pane, *view));
        static_cast<void>(workspace_.setActivePane(pane));
    } else {
        static_cast<void>(shell_.activateView(*view));
    }

    presentation_.sync();
    auto runtimeState = presentation_.viewRuntime(*view) != nullptr
                            ? *presentation_.viewRuntime(*view)
                            : app::ViewRuntimeState{};
    runtimeState.openMode = target.openMode;
    runtimeState.followEnabled = false;
    static_cast<void>(presentation_.setViewRuntime(*view, runtimeState));

    if (target.inputKind == app::RoutedInputKind::ExistingFile && !target.temporary) {
        static_cast<void>(lifecycle_.noteOpened(target.backingPath, target.openMode));
    }
    shell_.requestRefresh();
    return view;
}

void WxMainFrame::applyStartupPositions() {
    if (runtime_ == nullptr || pendingStartupPositions_.empty()) {
        return;
    }
    synchronizeNow();
    for (const auto& pending : pendingStartupPositions_) {
        std::error_code error;
        if (pending.position.hasColumn) {
            error = runtime_->goToPosition(
                pending.view, pending.position.line, pending.position.column);
        } else {
            error = runtime_->goToLine(pending.view, pending.position.line);
        }
        if (error) {
            startupLaunchIssues_.push_back(
                "Could not apply startup position " +
                std::to_string(pending.position.line) +
                (pending.position.hasColumn
                     ? ":" + std::to_string(pending.position.column)
                     : std::string{}) +
                ": " + error.message());
        }
    }
    pendingStartupPositions_.clear();
    static_cast<void>(lifecycle_.saveSession());
    scheduleSynchronize();
}

void WxMainFrame::showStartupRecoveryPrompt() {
    if (unclaimedRecoverySnapshots_.empty()) {
        return;
    }

    const auto count = static_cast<unsigned long long>(unclaimedRecoverySnapshots_.size());
    const auto message = wxString::Format(
        "%llu recovery snapshot(s) were found that are not referenced by the restored session.\n\n"
        "Recover them into tabs now? Choosing No keeps the snapshots untouched for a later "
        "startup.",
        count);
    if (wxMessageBox(message, "Recovery snapshots",
                     wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
        return;
    }

    if (const auto error = recoverUnclaimedSnapshots()) {
        showError("Recovery failed", error);
    }
}

std::error_code WxMainFrame::recoverUnclaimedSnapshots() {
    if (unclaimedRecoverySnapshots_.empty()) {
        return {};
    }

    std::vector<std::filesystem::path> remaining;
    remaining.reserve(unclaimedRecoverySnapshots_.size());
    std::error_code firstError;
    auto pane = workspace_.activePane();
    if (!pane) {
        pane = workspace_.primaryPane();
    }

    for (const auto& snapshotPath : unclaimedRecoverySnapshots_) {
        auto loaded = lifecycle_.recovery().load(snapshotPath);
        if (!loaded) {
            remaining.push_back(snapshotPath);
            if (!firstError) {
                firstError = loaded.error;
            }
            continue;
        }

        const auto documentId = documents_.createUntitled();
        auto* document = documents_.get(documentId);
        if (document == nullptr) {
            remaining.push_back(snapshotPath);
            if (!firstError) {
                firstError = std::make_error_code(std::errc::not_enough_memory);
            }
            continue;
        }

        auto recoveredAppearance = std::move(loaded.snapshot.appearance);
        auto error = document->restoreRecovered(std::move(loaded.snapshot.originalPath),
                                                std::move(loaded.snapshot.text),
                                                loaded.snapshot.encoding,
                                                loaded.snapshot.writeBom);
        if (!error) {
            auto& appearanceState = appearanceStates_[documentId];
            appearanceState.appearance = std::move(recoveredAppearance);
            appearanceState.loaded = true;
            ++appearanceState.revision;
            error = lifecycle_.recovery().checkpoint(
                documentId, *document, &appearanceState.appearance);
        }
        if (error) {
            static_cast<void>(documents_.close(documentId));
            remaining.push_back(snapshotPath);
            if (!firstError) {
                firstError = error;
            }
            continue;
        }

        const auto view = workspace_.openView(documentId, pane);
        if (!view) {
            static_cast<void>(lifecycle_.recovery().discard(documentId));
            static_cast<void>(documents_.close(documentId));
            remaining.push_back(snapshotPath);
            if (!firstError) {
                firstError = std::make_error_code(std::errc::state_not_recoverable);
            }
            continue;
        }

        lifecycle_.noteEdited(documentId);
        presentation_.setRecovered(documentId, true);
        static_cast<void>(workspace_.setActiveView(pane, view));
        static_cast<void>(workspace_.setActivePane(pane));

        if (snapshotPath != lifecycle_.recovery().snapshotPath(documentId)) {
            const auto discardError = lifecycle_.recovery().discardSnapshot(snapshotPath);
            if (discardError) {
                remaining.push_back(snapshotPath);
                if (!firstError) {
                    firstError = discardError;
                }
            }
        }
    }

    unclaimedRecoverySnapshots_ = std::move(remaining);
    presentation_.sync();
    static_cast<void>(lifecycle_.saveSession());
    shell_.requestRefresh();
    scheduleSynchronize();
    return firstError;
}

std::error_code WxMainFrame::discardActiveRecovery() {
    const auto documentId = activeDocumentId();
    auto* document = activeDocument();
    if (!documentId || document == nullptr || !presentation_.recovered(*documentId)) {
        return std::make_error_code(std::errc::operation_not_permitted);
    }
    if (!document->path().empty()) {
        return reloadActive();
    }

    if (wxMessageBox(
            "Discard the recovered untitled text? This cannot be undone after the recovery "
            "snapshot is removed.",
            "Discard recovery", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
        return {};
    }

    document->clear();
    const auto error = lifecycle_.forget(*documentId);
    if (error) {
        return error;
    }
    presentation_.setRecovered(*documentId, false);
    if (runtime_ != nullptr) {
        runtime_->invalidateDocument(*documentId);
    }
    static_cast<void>(lifecycle_.saveSession());
    shell_.requestRefresh();
    scheduleSynchronize();
    return {};
}

void WxMainFrame::createChrome() {
    SetBackgroundColour(theme_.window);

    titleBar_ = new WxTitleBar(*this,
                               theme_,
                               settings_.density == settings::UiDensity::Compact);

#ifdef __WXGTK__
    resizeTop_ = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    resizeBottom_ = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    resizeLeft_ = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    resizeRight_ = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);

    resizeTop_->SetCursor(wxCursor(wxCURSOR_SIZENS));
    resizeBottom_->SetCursor(wxCursor(wxCURSOR_SIZENS));
    resizeLeft_->SetCursor(wxCursor(wxCURSOR_SIZEWE));
    resizeRight_->SetCursor(wxCursor(wxCURSOR_SIZEWE));

    configureGtkResizeZone(*resizeTop_, GtkResizeZoneKind::Top);
    configureGtkResizeZone(*resizeBottom_, GtkResizeZoneKind::Bottom);
    configureGtkResizeZone(*resizeLeft_, GtkResizeZoneKind::Left);
    configureGtkResizeZone(*resizeRight_, GtkResizeZoneKind::Right);
#endif

    topBar_ = new WxChromeStrip(this, theme_, ChromeRuleEdge::Bottom);
    auto* topSizer = new wxBoxSizer(wxHORIZONTAL);
    fileButton_ = new wxButton(topBar_, wxID_ANY, "File", wxDefaultPosition, wxDefaultSize,
                               wxBU_EXACTFIT | wxBORDER_NONE);
    editButton_ = new wxButton(topBar_, wxID_ANY, "Edit", wxDefaultPosition, wxDefaultSize,
                               wxBU_EXACTFIT | wxBORDER_NONE);
    viewButton_ = new wxButton(topBar_, wxID_ANY, "View", wxDefaultPosition, wxDefaultSize,
                               wxBU_EXACTFIT | wxBORDER_NONE);
    formatButton_ = new wxButton(topBar_, wxID_ANY, "Format", wxDefaultPosition, wxDefaultSize,
                                 wxBU_EXACTFIT | wxBORDER_NONE);
    brandText_ = new wxStaticText(topBar_, wxID_ANY, "fasaFiso / text");

#ifdef __WXGTK__
    const auto linuxChrome = linuxChromeMetrics();
    const auto sizeMenuButton = [linuxChrome](wxButton* button) {
        if (button == nullptr) {
            return;
        }
        const auto label = button->GetTextExtent(button->GetLabel());
        button->SetMinSize(wxSize(
            label.GetWidth() + linuxChrome.menuPaddingX * 2,
            -1));
        applyGtkCssClass(*button, "nff-menu-button");
    };
    sizeMenuButton(fileButton_);
    sizeMenuButton(editButton_);
    sizeMenuButton(viewButton_);
    sizeMenuButton(formatButton_);
#endif

#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
    fileButton_->SetName("menu.file");
    editButton_->SetName("menu.edit");
    viewButton_->SetName("menu.view");
    formatButton_->SetName("menu.format");
#endif

#ifdef __WXGTK__
    const auto linuxChromeSpacing = linuxChromeMetrics();
    topSizer->AddSpacer(6);
    topSizer->Add(fileButton_, 0, wxEXPAND);
    topSizer->AddSpacer(linuxChromeSpacing.menuGap);
    topSizer->Add(editButton_, 0, wxEXPAND);
    topSizer->AddSpacer(linuxChromeSpacing.menuGap);
    topSizer->Add(viewButton_, 0, wxEXPAND);
    topSizer->AddSpacer(linuxChromeSpacing.menuGap);
    topSizer->Add(formatButton_, 0, wxEXPAND);
#else
    topSizer->Add(fileButton_, 0, wxEXPAND | wxLEFT, 6);
    topSizer->Add(editButton_, 0, wxEXPAND);
    topSizer->Add(viewButton_, 0, wxEXPAND);
    topSizer->Add(formatButton_, 0, wxEXPAND);
#endif
    topSizer->AddStretchSpacer(1);
    topSizer->Add(brandText_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    topBar_->SetSizer(topSizer);

    fileButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { showFileMenu(*fileButton_); });
    editButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { showEditMenu(*editButton_); });
    viewButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { showViewMenu(*viewButton_); });
    formatButton_->Bind(wxEVT_BUTTON,
                        [this](wxCommandEvent&) { showFormatMenu(*formatButton_); });

    sidebarSearch_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                    wxDefaultSize, wxTE_PROCESS_ENTER | wxBORDER_NONE);
    sidebarSearch_->SetHint("Search files...");
    sidebarSearch_->SetMargins(wxPoint(8, -1));
    sidebarSearch_->SetToolTip("Search the selected folder. Press F5 to build or refresh the index.");
    sidebarSearch_->Bind(wxEVT_TEXT, &WxMainFrame::onSidebarQuery, this);
    sidebarSearch_->Bind(wxEVT_KEY_DOWN, &WxMainFrame::onSidebarSearchKeyDown, this);

    sidebarTree_ = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                  wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_NO_LINES |
                                      wxBORDER_NONE);
    sidebarTree_->Bind(wxEVT_TREE_ITEM_ACTIVATED, &WxMainFrame::onSidebarTreeActivated, this);
    sidebarTree_->Bind(wxEVT_TREE_ITEM_EXPANDING, &WxMainFrame::onSidebarTreeExpanding, this);
    sidebarTree_->Bind(wxEVT_TREE_SEL_CHANGED, &WxMainFrame::onSidebarTreeSelectionChanged, this);
    sidebarTree_->Bind(wxEVT_LEFT_DOWN, &WxMainFrame::onSidebarTreeLeftDown, this);
    sidebarTree_->Bind(wxEVT_MOTION, &WxMainFrame::onSidebarTreeMotion, this);
    sidebarTree_->Bind(wxEVT_LEFT_UP, &WxMainFrame::onSidebarTreeLeftUp, this);
    sidebarTree_->Bind(wxEVT_MOUSE_CAPTURE_LOST, &WxMainFrame::onSidebarTreeCaptureLost, this);

    sidebarSplitter_ = new WxSplitterHandle(this, theme_, SplitterHandleAxis::Vertical);
    sidebarSplitter_->SetCursor(wxCursor(wxCURSOR_SIZEWE));
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
    sidebarSearch_->SetName("sidebar.search");
    sidebarTree_->SetName("sidebar.tree");
    sidebarSplitter_->SetName("sidebar.splitter");
#endif
    sidebarSplitter_->Bind(wxEVT_LEFT_DOWN, &WxMainFrame::onSidebarSplitterDown, this);
    sidebarSplitter_->Bind(wxEVT_MOTION, &WxMainFrame::onSidebarSplitterMotion, this);
    sidebarSplitter_->Bind(wxEVT_LEFT_UP, &WxMainFrame::onSidebarSplitterUp, this);
    sidebarSplitter_->Bind(wxEVT_MOUSE_CAPTURE_LOST, &WxMainFrame::onTabCaptureLost, this);

    externalNoticePanel_ = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxBORDER_NONE);
    auto* externalSizer = new wxBoxSizer(wxHORIZONTAL);
    externalNoticeText_ = new wxStaticText(externalNoticePanel_, wxID_ANY, wxEmptyString,
                                           wxDefaultPosition, wxDefaultSize,
                                           wxST_ELLIPSIZE_END);
    externalReloadButton_ = new wxButton(externalNoticePanel_, wxID_ANY, "Reload",
                                         wxDefaultPosition, wxDefaultSize,
                                         wxBU_EXACTFIT | wxBORDER_NONE);
    externalSaveMineButton_ = new wxButton(externalNoticePanel_, wxID_ANY, "Save Mine",
                                           wxDefaultPosition, wxDefaultSize,
                                           wxBU_EXACTFIT | wxBORDER_NONE);
    externalSaveAsButton_ = new wxButton(externalNoticePanel_, wxID_ANY, "Save As...",
                                         wxDefaultPosition, wxDefaultSize,
                                         wxBU_EXACTFIT | wxBORDER_NONE);
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
    externalNoticePanel_->SetName("notice.external");
    externalReloadButton_->SetName("notice.reload");
    externalSaveMineButton_->SetName("notice.save_mine");
    externalSaveAsButton_->SetName("notice.save_as");
#endif
    externalSizer->Add(externalNoticeText_, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 10);
    externalSizer->Add(externalReloadButton_, 0, wxEXPAND | wxLEFT, 4);
    externalSizer->Add(externalSaveMineButton_, 0, wxEXPAND | wxLEFT, 4);
    externalSizer->Add(externalSaveAsButton_, 0, wxEXPAND | wxLEFT | wxRIGHT, 4);
    externalNoticePanel_->SetSizer(externalSizer);
    externalNoticePanel_->Hide();

    externalReloadButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        const auto documentId = activeDocumentId();
        const auto* document = activeDocument();
        const bool discardUntitled = documentId && document != nullptr &&
                                     document->path().empty() &&
                                     presentation_.recovered(*documentId);
        const auto error = discardUntitled ? discardActiveRecovery() : reloadActive();
        if (error) {
            showError(discardUntitled ? "Discard recovery failed" : "Reload failed", error);
        }
        scheduleSynchronize();
    });
    externalSaveMineButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        const auto error = saveActiveOverwriteExternal();
        if (error) {
            showError("Save failed", error);
        }
        scheduleSynchronize();
    });
    externalSaveAsButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        const auto error = saveActiveAs(false);
        if (error) {
            showError("Save As failed", error);
        }
        scheduleSynchronize();
    });
    statusPanel_ = new WxChromeStrip(this, theme_, ChromeRuleEdge::Top);
    auto* statusSizer = new wxBoxSizer(wxHORIZONTAL);
    statusPositionText_ = new wxStaticText(statusPanel_, wxID_ANY, wxEmptyString);
    statusEncodingText_ = new wxStaticText(statusPanel_, wxID_ANY, wxEmptyString);
    statusLineEndingText_ = new wxStaticText(statusPanel_, wxID_ANY, wxEmptyString);
    statusFormatText_ = new wxStaticText(statusPanel_, wxID_ANY, wxEmptyString);
    statusModeText_ = new wxStaticText(statusPanel_, wxID_ANY, wxEmptyString);
    statusExtraText_ = new wxStaticText(statusPanel_, wxID_ANY, wxEmptyString,
                                        wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
    statusPositionText_->SetName("status.position");
    statusEncodingText_->SetName("status.encoding");
    statusLineEndingText_->SetName("status.line_ending");
    statusFormatText_->SetName("status.format");
    statusModeText_->SetName("status.mode");
    statusExtraText_->SetName("status.extra");
#endif
    statusSizer->Add(statusPositionText_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10);
    statusSizer->AddStretchSpacer(1);
    statusSizer->Add(statusEncodingText_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10);
    statusSizer->Add(statusLineEndingText_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10);
    statusSizer->Add(statusFormatText_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10);
    statusSizer->Add(statusModeText_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10);
    statusSizer->Add(statusExtraText_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 10);
    statusPanel_->SetSizer(statusSizer);

    updateThemeControls();
    resetSidebarTree();
}

void WxMainFrame::bindCommands() {
    const auto bind = [this](const app::CommandId id, app::CommandDispatcher::Handler handler) {
        static_cast<void>(dispatcher_.bind(id, std::move(handler)));
    };

    bind(app::CommandId::NewDocument, [this]() { return newDocument(); });
    bind(app::CommandId::OpenFile, [this]() { return openFilesDialog(); });
    bind(app::CommandId::Settings, [this]() { return showSettingsDialog(); });
    bind(app::CommandId::SidebarChooseFolder, [this]() { return chooseSidebarFolder(); });
    bind(app::CommandId::SidebarReindexFolder, [this]() { return reindexSidebarFolder(); });
    bind(app::CommandId::SidebarClearFolder, [this]() { return clearSidebarFolder(); });
    bind(app::CommandId::Save, [this]() { return saveActive(); });
    bind(app::CommandId::SaveAs, [this]() { return saveActiveAs(false); });
    bind(app::CommandId::SaveCopy, [this]() { return saveActiveAs(true); });
    bind(app::CommandId::Reload, [this]() { return reloadActive(); });
    bind(app::CommandId::ReopenAsUtf8, [this]() {
        return reopenActiveAsEncoding(encoding::Encoding::Utf8);
    });
    bind(app::CommandId::ReopenAsUtf16LE, [this]() {
        return reopenActiveAsEncoding(encoding::Encoding::Utf16LE);
    });
    bind(app::CommandId::ReopenAsUtf16BE, [this]() {
        return reopenActiveAsEncoding(encoding::Encoding::Utf16BE);
    });
    bind(app::CommandId::ReopenAsUtf32LE, [this]() {
        return reopenActiveAsEncoding(encoding::Encoding::Utf32LE);
    });
    bind(app::CommandId::ReopenAsUtf32BE, [this]() {
        return reopenActiveAsEncoding(encoding::Encoding::Utf32BE);
    });
    bind(app::CommandId::ReopenAsWindows1252, [this]() {
        return reopenActiveAsEncoding(encoding::Encoding::Windows1252);
    });
    bind(app::CommandId::ReopenAsWindows1254, [this]() {
        return reopenActiveAsEncoding(encoding::Encoding::Windows1254);
    });
    bind(app::CommandId::CloseView, [this]() { return closeActiveView(); });
    bind(app::CommandId::ClosePane, [this]() { return closeActivePane(); });
    bind(app::CommandId::Undo, [this]() { return editorCommand(EditorHostCommand::Undo); });
    bind(app::CommandId::Redo, [this]() { return editorCommand(EditorHostCommand::Redo); });
    bind(app::CommandId::Cut, [this]() { return editorCommand(EditorHostCommand::Cut); });
    bind(app::CommandId::Copy, [this]() { return editorCommand(EditorHostCommand::Copy); });
    bind(app::CommandId::Paste, [this]() { return editorCommand(EditorHostCommand::Paste); });
    bind(app::CommandId::SelectAll, [this]() { return editorCommand(EditorHostCommand::SelectAll); });
    bind(app::CommandId::Find, [this]() { return showSearchDialog(false); });
    bind(app::CommandId::FindNext,
         [this]() { return findFromSession(search::SearchDirection::Forward); });
    bind(app::CommandId::FindPrevious,
         [this]() { return findFromSession(search::SearchDirection::Backward); });
    bind(app::CommandId::Replace, [this]() { return showSearchDialog(true); });
    bind(app::CommandId::GoToLine, [this]() { return showGoToLineDialog(); });
    bind(app::CommandId::SplitHorizontal,
         [this]() { return splitActive(workspace::SplitOrientation::Horizontal); });
    bind(app::CommandId::SplitVertical,
         [this]() { return splitActive(workspace::SplitOrientation::Vertical); });
    bind(app::CommandId::ToggleSidebar, [this]() { return toggleSidebar(); });
    bind(app::CommandId::ToggleTabs, [this]() { return toggleTabs(); });
    bind(app::CommandId::ToggleWordWrap, [this]() { return toggleWordWrap(); });
    bind(app::CommandId::ToggleLineNumbers, [this]() { return toggleLineNumbers(); });
    bind(app::CommandId::ToggleLinkDetection, [this]() { return toggleLinkDetection(); });
    bind(app::CommandId::SwitchToEditor, [this]() { return switchActiveToEditor(); });
    bind(app::CommandId::SwitchToViewer, [this]() { return switchActiveToViewer(); });
    bind(app::CommandId::SwitchToHex, [this]() { return switchActiveToHexPreview(); });
    bind(app::CommandId::ToggleFollow, [this]() { return toggleFollow(); });
    bind(app::CommandId::ViewerPerformanceAutomatic, [this]() {
        return setViewerPerformance(viewer::PerformanceProfile::Automatic);
    });
    bind(app::CommandId::ViewerPerformanceFast, [this]() {
        return setViewerPerformance(viewer::PerformanceProfile::Fast);
    });
    bind(app::CommandId::ViewerPerformanceMemorySaver, [this]() {
        return setViewerPerformance(viewer::PerformanceProfile::MemorySaver);
    });
    bind(app::CommandId::EncodingUtf8, [this]() {
        return setDocumentEncoding(encoding::Encoding::Utf8);
    });
    bind(app::CommandId::EncodingUtf16LE, [this]() {
        return setDocumentEncoding(encoding::Encoding::Utf16LE);
    });
    bind(app::CommandId::EncodingUtf16BE, [this]() {
        return setDocumentEncoding(encoding::Encoding::Utf16BE);
    });
    bind(app::CommandId::EncodingUtf32LE, [this]() {
        return setDocumentEncoding(encoding::Encoding::Utf32LE);
    });
    bind(app::CommandId::EncodingUtf32BE, [this]() {
        return setDocumentEncoding(encoding::Encoding::Utf32BE);
    });
    bind(app::CommandId::EncodingWindows1252, [this]() {
        return setDocumentEncoding(encoding::Encoding::Windows1252);
    });
    bind(app::CommandId::EncodingWindows1254, [this]() {
        return setDocumentEncoding(encoding::Encoding::Windows1254);
    });
    bind(app::CommandId::ToggleBom, [this]() { return toggleDocumentBom(); });
    bind(app::CommandId::LineEndingLF, [this]() {
        return convertLineEndings(core::LineEndingPolicy::LF);
    });
    bind(app::CommandId::LineEndingCRLF, [this]() {
        return convertLineEndings(core::LineEndingPolicy::CRLF);
    });
    bind(app::CommandId::LineEndingCR, [this]() {
        return convertLineEndings(core::LineEndingPolicy::CR);
    });
    bind(app::CommandId::FormatValidate, [this]() { return validateActiveJson(); });
    bind(app::CommandId::FormatPretty, [this]() { return formatActiveJson(true); });
    bind(app::CommandId::FormatMinify, [this]() { return formatActiveJson(false); });
    bind(app::CommandId::ConvertExport, [this]() { return showConvertExportDialog(); });
    bind(app::CommandId::TextTrimTrailingWhitespace, [this]() {
        return applyTextUtility(app::CommandId::TextTrimTrailingWhitespace);
    });
    bind(app::CommandId::TextRemoveEmptyLines, [this]() {
        return applyTextUtility(app::CommandId::TextRemoveEmptyLines);
    });
    bind(app::CommandId::TextRemoveDuplicateLines, [this]() {
        return applyTextUtility(app::CommandId::TextRemoveDuplicateLines);
    });
    bind(app::CommandId::TextSortAscending, [this]() {
        return applyTextUtility(app::CommandId::TextSortAscending);
    });
    bind(app::CommandId::TextSortDescending, [this]() {
        return applyTextUtility(app::CommandId::TextSortDescending);
    });
    bind(app::CommandId::TextReverseLines, [this]() {
        return applyTextUtility(app::CommandId::TextReverseLines);
    });
    bind(app::CommandId::TextTabsToSpaces, [this]() {
        return applyTextUtility(app::CommandId::TextTabsToSpaces);
    });
    bind(app::CommandId::TextSpacesToTabs, [this]() {
        return applyTextUtility(app::CommandId::TextSpacesToTabs);
    });
    bind(app::CommandId::TextLowercaseAscii, [this]() {
        return applyTextUtility(app::CommandId::TextLowercaseAscii);
    });
    bind(app::CommandId::TextUppercaseAscii, [this]() {
        return applyTextUtility(app::CommandId::TextUppercaseAscii);
    });
    bind(app::CommandId::TextColorChoose, [this]() { return chooseSelectionTextColor(); });
    bind(app::CommandId::TextColorClear, [this]() { return clearSelectionTextColor(); });
    bind(app::CommandId::SelectionFontFamilyChoose,
         [this]() { return chooseSelectionFontFamily(); });
    bind(app::CommandId::SelectionFontFamilyClear,
         [this]() { return clearSelectionFontFamily(); });
    bind(app::CommandId::SelectionFontSizeChoose,
         [this]() { return chooseSelectionFontSize(); });
    bind(app::CommandId::SelectionFontSizeClear,
         [this]() { return clearSelectionFontSize(); });
    bind(app::CommandId::SelectionSpoilerSet,
         [this]() { return setSelectionSpoiler(true); });
    bind(app::CommandId::SelectionSpoilerClear,
         [this]() { return setSelectionSpoiler(false); });
    bind(app::CommandId::SelectionAppearanceReset,
         [this]() { return resetSelectionAppearance(); });

    const std::array bindings{
        std::pair{UiNew, app::CommandId::NewDocument},
        std::pair{UiOpen, app::CommandId::OpenFile},
        std::pair{UiSettings, app::CommandId::Settings},
        std::pair{UiSidebarChooseFolder, app::CommandId::SidebarChooseFolder},
        std::pair{UiSidebarReindexFolder, app::CommandId::SidebarReindexFolder},
        std::pair{UiSidebarClearFolder, app::CommandId::SidebarClearFolder},
        std::pair{UiSave, app::CommandId::Save},
        std::pair{UiSaveAs, app::CommandId::SaveAs},
        std::pair{UiSaveCopy, app::CommandId::SaveCopy},
        std::pair{UiReload, app::CommandId::Reload},
        std::pair{UiReopenAsUtf8, app::CommandId::ReopenAsUtf8},
        std::pair{UiReopenAsUtf16LE, app::CommandId::ReopenAsUtf16LE},
        std::pair{UiReopenAsUtf16BE, app::CommandId::ReopenAsUtf16BE},
        std::pair{UiReopenAsUtf32LE, app::CommandId::ReopenAsUtf32LE},
        std::pair{UiReopenAsUtf32BE, app::CommandId::ReopenAsUtf32BE},
        std::pair{UiReopenAsWindows1252, app::CommandId::ReopenAsWindows1252},
        std::pair{UiReopenAsWindows1254, app::CommandId::ReopenAsWindows1254},
        std::pair{UiCloseView, app::CommandId::CloseView},
        std::pair{UiClosePane, app::CommandId::ClosePane},
        std::pair{UiUndo, app::CommandId::Undo},
        std::pair{UiRedo, app::CommandId::Redo},
        std::pair{UiCut, app::CommandId::Cut},
        std::pair{UiCopy, app::CommandId::Copy},
        std::pair{UiPaste, app::CommandId::Paste},
        std::pair{UiSelectAll, app::CommandId::SelectAll},
        std::pair{UiFind, app::CommandId::Find},
        std::pair{UiFindNext, app::CommandId::FindNext},
        std::pair{UiFindPrevious, app::CommandId::FindPrevious},
        std::pair{UiReplace, app::CommandId::Replace},
        std::pair{UiGoToLine, app::CommandId::GoToLine},
        std::pair{UiSplitHorizontal, app::CommandId::SplitHorizontal},
        std::pair{UiSplitVertical, app::CommandId::SplitVertical},
        std::pair{UiToggleSidebar, app::CommandId::ToggleSidebar},
        std::pair{UiToggleTabs, app::CommandId::ToggleTabs},
        std::pair{UiToggleWrap, app::CommandId::ToggleWordWrap},
        std::pair{UiToggleLineNumbers, app::CommandId::ToggleLineNumbers},
        std::pair{UiToggleLinks, app::CommandId::ToggleLinkDetection},
        std::pair{UiSwitchToEditor, app::CommandId::SwitchToEditor},
        std::pair{UiSwitchToViewer, app::CommandId::SwitchToViewer},
        std::pair{UiSwitchToHex, app::CommandId::SwitchToHex},
        std::pair{UiToggleFollow, app::CommandId::ToggleFollow},
        std::pair{UiViewerPerformanceAutomatic, app::CommandId::ViewerPerformanceAutomatic},
        std::pair{UiViewerPerformanceFast, app::CommandId::ViewerPerformanceFast},
        std::pair{UiViewerPerformanceMemorySaver, app::CommandId::ViewerPerformanceMemorySaver},
        std::pair{UiEncodingUtf8, app::CommandId::EncodingUtf8},
        std::pair{UiEncodingUtf16LE, app::CommandId::EncodingUtf16LE},
        std::pair{UiEncodingUtf16BE, app::CommandId::EncodingUtf16BE},
        std::pair{UiEncodingUtf32LE, app::CommandId::EncodingUtf32LE},
        std::pair{UiEncodingUtf32BE, app::CommandId::EncodingUtf32BE},
        std::pair{UiEncodingWindows1252, app::CommandId::EncodingWindows1252},
        std::pair{UiEncodingWindows1254, app::CommandId::EncodingWindows1254},
        std::pair{UiToggleBom, app::CommandId::ToggleBom},
        std::pair{UiLineEndingLF, app::CommandId::LineEndingLF},
        std::pair{UiLineEndingCRLF, app::CommandId::LineEndingCRLF},
        std::pair{UiLineEndingCR, app::CommandId::LineEndingCR},
        std::pair{UiFormatValidate, app::CommandId::FormatValidate},
        std::pair{UiFormatPretty, app::CommandId::FormatPretty},
        std::pair{UiFormatMinify, app::CommandId::FormatMinify},
        std::pair{UiConvertExport, app::CommandId::ConvertExport},
        std::pair{UiTextTrimTrailingWhitespace, app::CommandId::TextTrimTrailingWhitespace},
        std::pair{UiTextRemoveEmptyLines, app::CommandId::TextRemoveEmptyLines},
        std::pair{UiTextRemoveDuplicateLines, app::CommandId::TextRemoveDuplicateLines},
        std::pair{UiTextSortAscending, app::CommandId::TextSortAscending},
        std::pair{UiTextSortDescending, app::CommandId::TextSortDescending},
        std::pair{UiTextReverseLines, app::CommandId::TextReverseLines},
        std::pair{UiTextTabsToSpaces, app::CommandId::TextTabsToSpaces},
        std::pair{UiTextSpacesToTabs, app::CommandId::TextSpacesToTabs},
        std::pair{UiTextLowercaseAscii, app::CommandId::TextLowercaseAscii},
        std::pair{UiTextUppercaseAscii, app::CommandId::TextUppercaseAscii},
        std::pair{UiTextColorChoose, app::CommandId::TextColorChoose},
        std::pair{UiTextColorClear, app::CommandId::TextColorClear},
        std::pair{UiSelectionFontFamilyChoose, app::CommandId::SelectionFontFamilyChoose},
        std::pair{UiSelectionFontFamilyClear, app::CommandId::SelectionFontFamilyClear},
        std::pair{UiSelectionFontSizeChoose, app::CommandId::SelectionFontSizeChoose},
        std::pair{UiSelectionFontSizeClear, app::CommandId::SelectionFontSizeClear},
        std::pair{UiSelectionSpoilerSet, app::CommandId::SelectionSpoilerSet},
        std::pair{UiSelectionSpoilerClear, app::CommandId::SelectionSpoilerClear},
        std::pair{UiSelectionAppearanceReset, app::CommandId::SelectionAppearanceReset},
    };
    for (const auto& [uiId, command] : bindings) {
        Bind(wxEVT_MENU, [this, command](wxCommandEvent&) { dispatch(command); }, uiId);
    }

    Bind(wxEVT_MENU, [this](wxCommandEvent&) { applyTheme(settings::ThemePreference::System); },
         UiThemeSystem);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { applyTheme(settings::ThemePreference::Light); },
         UiThemeLight);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { applyTheme(settings::ThemePreference::Dark); },
         UiThemeDark);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { applyAccent(settings::AccentPreference::Violet); },
         UiAccentViolet);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { applyAccent(settings::AccentPreference::Blue); },
         UiAccentBlue);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { applyAccent(settings::AccentPreference::Teal); },
         UiAccentTeal);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { applyAccent(settings::AccentPreference::Rose); },
         UiAccentRose);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { applyAccent(settings::AccentPreference::Amber); },
         UiAccentAmber);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { applyDensity(settings::UiDensity::Compact); },
         UiDensityCompact);
    Bind(wxEVT_MENU,
         [this](wxCommandEvent&) { applyDensity(settings::UiDensity::Comfortable); },
         UiDensityComfortable);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { chooseEditorFont(); }, UiChooseFont);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { setEditorFontSize(10.0); }, UiFontSize10);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { setEditorFontSize(11.0); }, UiFontSize11);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { setEditorFontSize(12.0); }, UiFontSize12);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { setEditorFontSize(13.0); }, UiFontSize13);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { setEditorFontSize(14.0); }, UiFontSize14);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { setEditorFontSize(16.0); }, UiFontSize16);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { setEditorFontSize(18.0); }, UiFontSize18);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { setEditorFontSize(20.0); }, UiFontSize20);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, UiQuit);
}

void WxMainFrame::installAccelerators() {
    std::array<wxAcceleratorEntry, 19> entries;
    entries[0].Set(wxACCEL_CTRL, 'N', UiNew);
    entries[1].Set(wxACCEL_CTRL, 'O', UiOpen);
    entries[2].Set(wxACCEL_CTRL, 'S', UiSave);
    entries[3].Set(wxACCEL_CTRL | wxACCEL_SHIFT, 'S', UiSaveAs);
    entries[4].Set(wxACCEL_CTRL, 'W', UiCloseView);
    entries[5].Set(wxACCEL_CTRL, 'Z', UiUndo);
    entries[6].Set(wxACCEL_CTRL, 'Y', UiRedo);
    entries[7].Set(wxACCEL_CTRL, 'X', UiCut);
    entries[8].Set(wxACCEL_CTRL, 'C', UiCopy);
    entries[9].Set(wxACCEL_CTRL, 'V', UiPaste);
    entries[10].Set(wxACCEL_CTRL, 'A', UiSelectAll);
    entries[11].Set(wxACCEL_CTRL, 'F', UiFind);
    entries[12].Set(wxACCEL_NORMAL, WXK_F3, UiFindNext);
    entries[13].Set(wxACCEL_SHIFT, WXK_F3, UiFindPrevious);
    entries[14].Set(wxACCEL_CTRL, 'H', UiReplace);
    entries[15].Set(wxACCEL_CTRL, 'G', UiGoToLine);
    entries[16].Set(wxACCEL_CTRL | wxACCEL_SHIFT, 'B', UiToggleSidebar);
    entries[17].Set(wxACCEL_NORMAL, WXK_F5, UiSidebarReindexFolder);
    entries[18].Set(wxACCEL_CTRL | wxACCEL_SHIFT, 'C', UiTextColorChoose);
    SetAcceleratorTable(wxAcceleratorTable(static_cast<int>(entries.size()), entries.data()));
}

void WxMainFrame::setupBranding() {
    wxMemoryInputStream stream(embedded::kTrayPng, embedded::kTrayPngSize);
    wxImage image;
    if (!image.LoadFile(stream, wxBITMAP_TYPE_PNG) || !image.IsOk()) {
        return;
    }

    auto frameImage = image.Copy();
    frameImage.Rescale(64, 64, wxIMAGE_QUALITY_HIGH);
    wxIcon frameIcon;
    frameIcon.CopyFromBitmap(wxBitmap(frameImage));
    if (frameIcon.IsOk()) {
        SetIcon(frameIcon);
    }

    if (titleBar_ != nullptr) {
        auto titleImage = image.Copy();
        const int titleIconSize =
            FromDIP(settings_.density == settings::UiDensity::Compact ? 16 : 18);
        titleImage.Rescale(titleIconSize, titleIconSize, wxIMAGE_QUALITY_HIGH);
        titleBar_->setIcon(wxBitmap(titleImage));
    }

    auto trayImage = image.Copy();
    trayImage.Rescale(32, 32, wxIMAGE_QUALITY_HIGH);
    const auto trayBundle = wxBitmapBundle::FromBitmap(wxBitmap(trayImage));
    trayIcon_ = std::make_unique<WxTrayIcon>(*this);
    if (!trayIcon_->install(trayBundle)) {
        trayIcon_.reset();
    }
}

void WxMainFrame::updateChrome(const GuiFrame& frame) {
    setBounds(titleBar_, frame.layout.titleBar);
    setBounds(topBar_, frame.layout.topBar);
    updateSidebar(frame);
    setBounds(externalNoticePanel_, frame.layout.externalNotice);
    updateExternalNotice(frame.presentation);
    setBounds(statusPanel_, frame.layout.statusBar);
    updateStatus(frame.presentation);
    updatePaneTabs(frame);
    updateWorkspaceSplitters(frame);
    updateWindowTitle(frame.presentation);
    updateResizeZones();
}

void WxMainFrame::updateStatus(const app::ApplicationPresentationSnapshot& presentation) {
    if (statusPanel_ == nullptr || statusPositionText_ == nullptr ||
        statusEncodingText_ == nullptr || statusLineEndingText_ == nullptr ||
        statusFormatText_ == nullptr || statusModeText_ == nullptr || statusExtraText_ == nullptr) {
        return;
    }
    const auto& status = presentation.status;
    const bool visibilityChanged = statusPanel_->IsShown() != status.visible;
    if (visibilityChanged) {
        statusPanel_->Show(status.visible);
    }
    if (!status.visible) {
        return;
    }

    bool layoutChanged = visibilityChanged;
    const auto setLabelIfChanged = [&layoutChanged](wxStaticText* target, const wxString& value) {
        if (target->GetLabel() == value) {
            return;
        }
        target->SetLabel(value);
        layoutChanged = true;
    };

    wxString position = wxString::Format("Ln %llu, Col %llu",
                                         static_cast<unsigned long long>(status.line),
                                         static_cast<unsigned long long>(status.column));
    if (status.selectionBytes != 0U) {
        position += wxString::Format("  Sel %llu B",
                                     static_cast<unsigned long long>(status.selectionBytes));
    }
    setLabelIfChanged(statusPositionText_, position);

    wxString encoding = fromUtf8(status.encoding);
    if (status.writesBom) {
        encoding += " + BOM";
    }
    setLabelIfChanged(statusEncodingText_, encoding);
    setLabelIfChanged(statusLineEndingText_, fromUtf8(statusValueForUi("EOL", status.lineEnding)));
    setLabelIfChanged(statusFormatText_, fromUtf8(status.format));

    wxString mode = fromUtf8(status.mode);
    if (!status.viewerPerformance.empty()) {
        mode += " / " + fromUtf8(status.viewerPerformance);
        if (!status.viewerResolvedPerformance.empty() &&
            status.viewerResolvedPerformance != status.viewerPerformance) {
            mode += " -> " + fromUtf8(status.viewerResolvedPerformance);
        }
    }
    setLabelIfChanged(statusModeText_, fromUtf8(statusValueForUi("Mode", toUtf8(mode))));

    wxString extra;
    const auto appendExtra = [&extra](const wxString& value) {
        if (!value.empty()) {
            if (!extra.empty()) extra += "   ";
            extra += value;
        }
    };
    if (status.followEnabled) {
        appendExtra(status.followWaiting ? wxString("Follow (waiting)") : wxString("Follow"));
    }
    if (status.mode == "View" || status.mode == "Hex") {
        appendExtra(formatByteSize(status.contentBytes));
        if (status.windowByteEnd > status.windowByteStart) {
            appendExtra(wxString("Window ") + formatByteSize(status.windowByteStart) + "-" +
                        formatByteSize(status.windowByteEnd));
        }
        if (status.mode == "View" && status.viewerCacheResidentBytes != 0U) {
            appendExtra(wxString("Cache ") +
                        formatByteSize(static_cast<std::uint64_t>(status.viewerCacheResidentBytes)));
        }
    }
    if (status.recovered) appendExtra("Recovered");
    else if (status.externalConflict) appendExtra("External change");
    setLabelIfChanged(statusExtraText_, extra);
    const bool extraVisible = !extra.empty();
    if (statusExtraText_->IsShown() != extraVisible) {
        statusExtraText_->Show(extraVisible);
        layoutChanged = true;
    }
    if (layoutChanged) {
        statusPanel_->Layout();
    }
}

void WxMainFrame::updateExternalNotice(
    const app::ApplicationPresentationSnapshot& presentation) {
    if (externalNoticePanel_ == nullptr || externalNoticeText_ == nullptr ||
        externalReloadButton_ == nullptr || externalSaveMineButton_ == nullptr ||
        externalSaveAsButton_ == nullptr) {
        return;
    }

    const auto& status = presentation.status;
    const bool visible = status.visible && (status.externalConflict || status.recovered);
    if (externalNoticePanel_->IsShown() != visible) {
        externalNoticePanel_->Show(visible);
    }
    if (!visible) {
        return;
    }

    const auto noticeSurface = status.recovered ? theme_.recoverySurface : theme_.warningSurface;
    const auto noticeText = status.recovered ? theme_.recoveryText : theme_.warningText;
    externalNoticePanel_->SetBackgroundColour(noticeSurface);
    externalNoticeText_->SetBackgroundColour(noticeSurface);
    externalNoticeText_->SetForegroundColour(noticeText);
    externalReloadButton_->SetBackgroundColour(noticeSurface);
    externalReloadButton_->SetForegroundColour(theme_.text);
    externalSaveMineButton_->SetBackgroundColour(theme_.accent);
    externalSaveMineButton_->SetForegroundColour(theme_.text);
    externalSaveAsButton_->SetBackgroundColour(noticeSurface);
    externalSaveAsButton_->SetForegroundColour(theme_.text);

    const auto* document = activeDocument();
    const bool hasDocument = document != nullptr;
    const bool hasPath = hasDocument && !document->path().empty();
    const bool canWriteText = hasDocument && document->textBufferLoaded();
    const bool diskReadable = status.externalChangeState != storage::FileChangeState::Deleted &&
                              status.externalChangeState != storage::FileChangeState::Inaccessible;

    if (status.recovered) {
        if (hasPath) {
            externalNoticeText_->SetLabel(
                "Recovered unsaved changes from the previous session. The disk file was not "
                "overwritten.");
            externalReloadButton_->SetLabel("Reload Disk");
            externalReloadButton_->Show(true);
            externalReloadButton_->Enable(diskReadable);
            externalSaveMineButton_->Show(true);
            externalSaveMineButton_->Enable(
                canWriteText && status.externalChangeState != storage::FileChangeState::Inaccessible);
            externalSaveAsButton_->Show(true);
            externalSaveAsButton_->Enable(canWriteText);
        } else {
            externalNoticeText_->SetLabel(
                "Recovered untitled text from the previous session. Save it or discard the "
                "recovery.");
            externalReloadButton_->SetLabel("Discard");
            externalReloadButton_->Show(true);
            externalReloadButton_->Enable(true);
            externalSaveMineButton_->Show(false);
            externalSaveAsButton_->Show(true);
            externalSaveAsButton_->Enable(canWriteText);
        }
        externalNoticePanel_->Layout();
        return;
    }

    externalReloadButton_->SetLabel("Reload");
    externalReloadButton_->Show(true);
    externalSaveMineButton_->Show(true);
    externalSaveAsButton_->Show(false);

    wxString message;
    if (status.explicitOverwriteRequired &&
        status.externalChangeState == storage::FileChangeState::Untracked) {
        message = "Recovered text needs confirmation before replacing the file on disk.";
    } else {
        switch (status.externalChangeState) {
        case storage::FileChangeState::Modified:
            message = "Changed on disk. Reload or explicitly save your version.";
            break;
        case storage::FileChangeState::Deleted:
            message = "Deleted on disk. Save Mine recreates the file.";
            break;
        case storage::FileChangeState::Inaccessible:
            message = "File is unavailable. Your open text is unchanged.";
            break;
        case storage::FileChangeState::Untracked:
        case storage::FileChangeState::Unchanged:
            message = "Saving this document to its original path requires confirmation.";
            break;
        }
    }
    externalNoticeText_->SetLabel(message);
    externalReloadButton_->Enable(hasPath && diskReadable);
    externalSaveMineButton_->Enable(
        hasPath && canWriteText &&
        status.externalChangeState != storage::FileChangeState::Inaccessible);
    externalNoticePanel_->Layout();
}

void WxMainFrame::updateSidebar(const GuiFrame& frame) {
    const bool visible = frame.presentation.sidebar.visible;
    const auto updateSidebarWindow = [visible](wxWindow* window, const Rect& bounds) {
        if (window == nullptr) return;
        if (visible) static_cast<void>(setBounds(window, bounds));
        if (window->IsShown() != visible) window->Show(visible);
    };
    updateSidebarWindow(sidebarSearch_, frame.layout.sidebarSearch);
    updateSidebarWindow(sidebarTree_, frame.layout.sidebarBody);
    updateSidebarWindow(sidebarSplitter_, frame.layout.sidebarSplitter);

    if (!visible || sidebarTree_ == nullptr) {
        return;
    }

    const auto scopeKey = pathUtf8(sidebarSearchScope());
    const auto sourceGeneration = fileSearch_.generation();
    if (sidebarGenerationCache_ == sourceGeneration &&
        sidebarQueryCache_ == sidebarSearchQuery_ && sidebarScopeCache_ == scopeKey) {
        return;
    }

    sidebarGenerationCache_ = sourceGeneration;
    sidebarQueryCache_ = sidebarSearchQuery_;
    sidebarScopeCache_ = scopeKey;

    if (sidebarSearchQuery_.empty()) {
        if (sidebarSearchTreeMode_) {
            resetSidebarTree();
        }
        return;
    }

    requestSidebarSearch(sidebarSearchQuery_);
}

void WxMainFrame::updatePaneTabs(const GuiFrame& frame) {
    const auto fingerprint = makeTabsFingerprint(frame.presentation);
    bool topologyMatches = tabsFingerprint_ && *tabsFingerprint_ == fingerprint;
    if (topologyMatches) {
        const auto expectedPaneCount =
            frame.presentation.tabsVisible ? frame.presentation.panes.size() : 0U;
        topologyMatches = tabPanels_.size() == expectedPaneCount;
        std::size_t widgetIndex = 0U;
        if (topologyMatches && frame.presentation.tabsVisible) {
            for (const auto& pane : frame.presentation.panes) {
                if (!tabPanels_.contains(pane.pane)) {
                    topologyMatches = false;
                    break;
                }
                for (const auto& tab : pane.tabs) {
                    if (widgetIndex >= tabWidgets_.size() ||
                        tabWidgets_[widgetIndex].pane != pane.pane ||
                        tabWidgets_[widgetIndex].view != tab.view) {
                        topologyMatches = false;
                        break;
                    }
                    ++widgetIndex;
                }
                if (!topologyMatches) {
                    break;
                }
            }
        }
        topologyMatches = topologyMatches && widgetIndex == tabWidgets_.size();
    }
    if (!topologyMatches) {
        cancelTabDrag();
        for (auto& [pane, panel] : tabPanels_) {
            static_cast<void>(pane);
            if (panel != nullptr) {
                panel->Hide();
                panel->Destroy();
            }
        }
        tabPanels_.clear();
        tabWidgets_.clear();
        tabAddButtons_.clear();
        tabsFingerprint_ = fingerprint;

        if (frame.presentation.tabsVisible) {
            const bool compact = settings_.density == settings::UiDensity::Compact;
            for (const auto& pane : frame.presentation.panes) {
                auto* panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                          wxBORDER_NONE);
                panel->SetBackgroundColour(blendColour(theme_.chrome, theme_.surface, 0.55));
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
                panel->SetName(fromUtf8("pane." + std::to_string(pane.pane.value)));
#endif
                panel->SetDropTarget(new WxFileDropTarget(
                    [this, paneId = pane.pane](
                        const std::vector<std::filesystem::path>& paths) {
                        return openDroppedFiles(paths, paneId);
                    }));
                auto* sizer = new wxBoxSizer(wxHORIZONTAL);
                for (const auto& tab : pane.tabs) {
                    const auto title = fromUtf8(tab.title);
                    const auto tooltip = tab.path.empty() ? title : fromUtf8(tab.path);
                    auto* control = new WxPolishedTab(
                        panel, title, tooltip, theme_, tab.active, tab.modified,
                        tab.externalConflict, compact,
                        [this, view = tab.view, paneId = pane.pane](wxMouseEvent& event) {
                            onTabLeftDown(event, view, paneId);
                        },
                        [this, view = tab.view]() {
                            CallAfter([this, view]() {
                                if (shuttingDown_ || workspace_.view(view) == nullptr) {
                                    return;
                                }
                                if (const auto error = closeView(view)) {
                                    showError("Close failed", error);
                                }
                                scheduleSynchronize();
                            });
                        },
                        [this, view = tab.view](wxMouseEvent& event) {
                            auto* anchor = static_cast<wxWindow*>(event.GetEventObject());
                            if (anchor != nullptr) {
                                static_cast<void>(shell_.activateView(view));
                                showTabContextMenu(*anchor, view);
                                scheduleSynchronize();
                            }
                            event.Skip(false);
                        });
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
                    control->SetName(fromUtf8("tab." + std::to_string(tab.view.value)));
#endif
                    control->SetDropTarget(new WxFileDropTarget(
                        [this, paneId = pane.pane](
                            const std::vector<std::filesystem::path>& paths) {
                            return openDroppedFiles(paths, paneId);
                        }));
                    tabWidgets_.push_back({control, pane.pane, tab.view});
                    sizer->Add(control, 0, wxEXPAND | wxRIGHT, 1);
                }
                auto* addTab = new WxTabAddButton(
                    panel, theme_, compact,
                    [this, paneId = pane.pane]() {
                        CallAfter([this, paneId]() {
                            if (shuttingDown_ || workspace_.pane(paneId) == nullptr) {
                                return;
                            }
                            if (const auto error = newDocumentInPane(paneId)) {
                                showError("New tab failed", error);
                            }
                            scheduleSynchronize();
                        });
                    });
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
                addTab->SetName(fromUtf8("pane." + std::to_string(pane.pane.value) + ".new"));
#endif
                sizer->Add(addTab, 0, wxEXPAND | wxLEFT, 2);
                panel->SetSizer(sizer);
                tabPanels_.emplace(pane.pane, panel);
                tabAddButtons_.emplace(pane.pane, addTab);
            }
        }
    }

    const bool compact = settings_.density == settings::UiDensity::Compact;
    std::size_t widgetIndex = 0U;
    for (const auto& pane : frame.presentation.panes) {
        bool contentLayoutChanged = false;
        for (const auto& tab : pane.tabs) {
            if (widgetIndex >= tabWidgets_.size()) {
                break;
            }
            auto& widget = tabWidgets_[widgetIndex++];
            auto* polished = dynamic_cast<WxPolishedTab*>(widget.button);
            if (polished == nullptr) {
                continue;
            }
            const auto title = fromUtf8(tab.title);
            const auto tooltip = tab.path.empty() ? title : fromUtf8(tab.path);
            contentLayoutChanged =
                polished->updatePresentation(title, tooltip, tab.active, tab.modified,
                                             tab.externalConflict, compact) || contentLayoutChanged;
        }
        const auto add = tabAddButtons_.find(pane.pane);
        if (add != tabAddButtons_.end()) {
            if (auto* button = dynamic_cast<WxTabAddButton*>(add->second);
                button != nullptr && button->setCompact(compact)) {
                contentLayoutChanged = true;
            }
        }
        if (contentLayoutChanged) {
            const auto panel = tabPanels_.find(pane.pane);
            if (panel != tabPanels_.end() && panel->second != nullptr) {
                panel->second->Layout();
            }
        }
    }

    for (const auto& layout : frame.layout.panes) {
        const auto iterator = tabPanels_.find(layout.pane);
        if (iterator == tabPanels_.end()) {
            continue;
        }
        const bool geometryChanged = setBounds(iterator->second, layout.tabStrip);
        const bool visible = frame.presentation.tabsVisible;
        const bool visibilityChanged = iterator->second->IsShown() != visible;
        if (visibilityChanged) iterator->second->Show(visible);
        if (geometryChanged || visibilityChanged) {
            iterator->second->Layout();
        }
    }
}

void WxMainFrame::updateWorkspaceSplitters(const GuiFrame& frame) {
    std::string fingerprint;
    for (const auto& splitter : frame.layout.splitters) {
        fingerprint += std::to_string(splitter.split.value);
        fingerprint += splitter.orientation == workspace::SplitOrientation::Horizontal ? 'H' : 'V';
        fingerprint += ';';
    }

    if (fingerprint != workspaceSplitterFingerprint_) {
        for (auto& widget : workspaceSplitterWidgets_) {
            if (widget.panel != nullptr) {
                widget.panel->Destroy();
            }
        }
        workspaceSplitterWidgets_.clear();
        workspaceSplitterFingerprint_ = std::move(fingerprint);

        for (const auto& splitter : frame.layout.splitters) {
            const auto axis = splitter.orientation == workspace::SplitOrientation::Horizontal
                                  ? SplitterHandleAxis::Vertical
                                  : SplitterHandleAxis::Horizontal;
            auto* panel = new WxSplitterHandle(this, theme_, axis);
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
            panel->SetName(fromUtf8("splitter." + std::to_string(splitter.split.value)));
#endif
            panel->SetCursor(wxCursor(splitter.orientation == workspace::SplitOrientation::Horizontal
                                          ? wxCURSOR_SIZEWE
                                          : wxCURSOR_SIZENS));
            panel->Bind(wxEVT_LEFT_DOWN,
                        [this, split = splitter.split](wxMouseEvent& event) {
                            onWorkspaceSplitterDown(event, split);
                        });
            workspaceSplitterWidgets_.push_back(
                {panel, splitter.split, splitter.orientation});
        }
    }

    for (std::size_t index = 0U;
         index < frame.layout.splitters.size() && index < workspaceSplitterWidgets_.size();
         ++index) {
        auto& widget = workspaceSplitterWidgets_[index];
        const bool geometryChanged = setBounds(widget.panel, frame.layout.splitters[index].bounds);
        const bool visibilityChanged = !widget.panel->IsShown();
        if (visibilityChanged) widget.panel->Show(true);
        if (geometryChanged || visibilityChanged) widget.panel->Raise();
    }
}

void WxMainFrame::updateWorkspaceSplitterPreview(const SplitterLayout& splitter,
                                                 const double ratio) {
    if (!std::isfinite(ratio)) {
        hideWorkspaceSplitterPreview();
        return;
    }

    if (workspaceSplitPreview_ == nullptr) {

        workspaceSplitPreview_ = new wxPopupWindow(this, wxBORDER_NONE);
    }

    const auto metrics = shell_.layoutMetrics();
    const bool horizontal = splitter.orientation == workspace::SplitOrientation::Horizontal;
    const double thickness = metrics.splitterThickness;
    const double available = horizontal
                                 ? std::max(0.0, splitter.track.width - thickness)
                                 : std::max(0.0, splitter.track.height - thickness);
    const double minimum = horizontal ? metrics.minimumPaneWidth : metrics.minimumPaneHeight;

    double firstExtent = 0.0;
    if (available > 0.0) {
        if (available < minimum * 2.0) {
            firstExtent = available * 0.5;
        } else {
            firstExtent = std::clamp(available * ratio, minimum, available - minimum);
        }
    }

    auto preview = splitter.bounds;
    constexpr double previewThickness = 2.0;
    if (horizontal) {
        preview.x = splitter.track.x + firstExtent +
                    std::max(0.0, (splitter.bounds.width - previewThickness) * 0.5);
        preview.width = std::min(previewThickness, splitter.bounds.width);
    } else {
        preview.y = splitter.track.y + firstExtent +
                    std::max(0.0, (splitter.bounds.height - previewThickness) * 0.5);
        preview.height = std::min(previewThickness, splitter.bounds.height);
    }

    const auto pointer = ScreenToClient(wxGetMousePosition());
    preview = localSplitterDragPreview(
        preview,
        Point{static_cast<double>(pointer.x), static_cast<double>(pointer.y)},
        horizontal,
        previewThickness,
        72.0);

    const auto screen = ClientToScreen(wxPoint(toPixel(preview.x), toPixel(preview.y)));
    workspaceSplitPreview_->SetBackgroundColour(
        blendColour(theme_.splitter, theme_.accent, 0.58));
    workspaceSplitPreview_->SetSize(screen.x,
                                    screen.y,
                                    std::max(1, toPixel(preview.width)),
                                    std::max(1, toPixel(preview.height)),
                                    wxSIZE_FORCE);
    workspaceSplitPreview_->Show(true);
    workspaceSplitPreview_->Raise();
    workspaceSplitPreview_->Refresh(false);
}

void WxMainFrame::hideWorkspaceSplitterPreview() noexcept {
    if (workspaceSplitPreview_ != nullptr && workspaceSplitPreview_->IsShown()) {
        workspaceSplitPreview_->Hide();
    }
}

void WxMainFrame::updateSidebarSplitterPreview(const double width) {
    if (!std::isfinite(width)) {
        hideSidebarSplitterPreview();
        return;
    }
    const auto& layout = shell_.frame().layout;
    if (layout.sidebarSplitter.empty() || layout.contentFrame.empty()) {
        hideSidebarSplitterPreview();
        return;
    }
    if (sidebarSplitPreview_ == nullptr) {
        sidebarSplitPreview_ = new wxPopupWindow(this, wxBORDER_NONE);
    }

    auto preview = layout.sidebarSplitter;
    constexpr double previewThickness = 2.0;
    preview.x = layout.contentFrame.x + width +
                std::max(0.0, (layout.sidebarSplitter.width - previewThickness) * 0.5);
    preview.width = std::min(previewThickness, layout.sidebarSplitter.width);
    const auto screen = ClientToScreen(wxPoint(toPixel(preview.x), toPixel(preview.y)));
    sidebarSplitPreview_->SetBackgroundColour(theme_.accent);
    sidebarSplitPreview_->SetSize(screen.x,
                                  screen.y,
                                  std::max(1, toPixel(preview.width)),
                                  std::max(1, toPixel(preview.height)),
                                  wxSIZE_FORCE);
    sidebarSplitPreview_->Show(true);
    sidebarSplitPreview_->Raise();
    sidebarSplitPreview_->Refresh(false);
}

void WxMainFrame::hideSidebarSplitterPreview() noexcept {
    if (sidebarSplitPreview_ != nullptr && sidebarSplitPreview_->IsShown()) {
        sidebarSplitPreview_->Hide();
    }
}

void WxMainFrame::updateThemeControls() {
    SetBackgroundColour(theme_.window);
    const auto applyPanel = [this](wxWindow* window, const wxColour& background) {
        if (window == nullptr) {
            return;
        }
        window->SetBackgroundColour(background);
        window->SetForegroundColour(theme_.text);
    };

#ifdef __WXGTK__
    applyGtkThemeCss(theme_);
#endif
    if (titleBar_ != nullptr) {
        titleBar_->applyTheme(theme_);
    }
    if (auto* strip = dynamic_cast<WxChromeStrip*>(topBar_)) {
        strip->applyTheme(theme_);
    } else {
        applyPanel(topBar_, theme_.chrome);
    }
    applyPanel(fileButton_, theme_.chrome);
    applyPanel(editButton_, theme_.chrome);
    applyPanel(viewButton_, theme_.chrome);
    applyPanel(formatButton_, theme_.chrome);
    applyPanel(brandText_, theme_.chrome);
    applyPanel(sidebarSearch_, theme_.surfaceRaised);
    applyPanel(sidebarTree_, theme_.surface);
    if (auto* handle = dynamic_cast<WxSplitterHandle*>(sidebarSplitter_)) {
        handle->applyTheme(theme_);
    } else {
        applyPanel(sidebarSplitter_, theme_.splitter);
    }
    applyPanel(externalNoticePanel_, theme_.surfaceRaised);
    applyPanel(externalNoticeText_, theme_.surfaceRaised);
    applyPanel(externalReloadButton_, theme_.surfaceRaised);
    applyPanel(externalSaveMineButton_, theme_.accent);
    applyPanel(externalSaveAsButton_, theme_.surfaceRaised);
    if (auto* strip = dynamic_cast<WxChromeStrip*>(statusPanel_)) {
        strip->applyTheme(theme_);
    } else {
        applyPanel(statusPanel_, theme_.chrome);
    }
    applyPanel(statusPositionText_, theme_.chrome);
    applyPanel(statusEncodingText_, theme_.chrome);
    applyPanel(statusLineEndingText_, theme_.chrome);
    applyPanel(statusFormatText_, theme_.chrome);
    applyPanel(statusModeText_, theme_.chrome);
    applyPanel(statusExtraText_, theme_.chrome);
    applyPanel(resizeTop_, theme_.frame);
    applyPanel(resizeBottom_, theme_.frame);
    applyPanel(resizeLeft_, theme_.frame);
    applyPanel(resizeRight_, theme_.frame);
    SetBackgroundColour(theme_.frame);

    if (brandText_ != nullptr) {
        brandText_->SetForegroundColour(blendColour(theme_.mutedText, theme_.window, 0.18));
    }
    if (externalSaveMineButton_ != nullptr) {
        externalSaveMineButton_->SetForegroundColour(theme_.text);
    }
    const auto styleStatus = [this](wxStaticText* text, const bool primary) {
        if (text != nullptr) {
            text->SetForegroundColour(primary ? theme_.text : theme_.mutedText);
        }
    };
    styleStatus(statusPositionText_, true);
    styleStatus(statusEncodingText_, false);
    styleStatus(statusLineEndingText_, false);
    styleStatus(statusFormatText_, false);
    styleStatus(statusModeText_, false);
    styleStatus(statusExtraText_, false);

    for (auto& [pane, panel] : tabPanels_) {
        static_cast<void>(pane);
        applyPanel(panel, blendColour(theme_.chrome, theme_.surface, 0.55));
    }
    for (auto& tab : tabWidgets_) {
        if (auto* polished = dynamic_cast<WxPolishedTab*>(tab.button)) {
            polished->applyTheme(theme_);
        }
    }
    for (auto& [pane, button] : tabAddButtons_) {
        static_cast<void>(pane);
        if (auto* add = dynamic_cast<WxTabAddButton*>(button)) {
            add->applyTheme(theme_);
        }
    }
    for (auto& widget : workspaceSplitterWidgets_) {
        if (auto* handle = dynamic_cast<WxSplitterHandle*>(widget.panel)) {
            handle->applyTheme(theme_);
        } else {
            applyPanel(widget.panel, theme_.splitter);
        }
    }
    Refresh(false);
}

void WxMainFrame::applyLayoutDensity() {
    ShellLayoutMetrics metrics;
    metrics.outerFrameInset = (IsMaximized() || IsFullScreen()) ? 0.0 : 3.0;
    if (settings_.density == settings::UiDensity::Compact) {
        metrics.titleBarHeight = 30.0;
        metrics.topBarHeight = 32.0;
        metrics.sidebarSearchHeight = 36.0;
        metrics.tabStripHeight = 30.0;
        metrics.externalNoticeHeight = 34.0;
        metrics.statusBarHeight = 24.0;
    } else {
        metrics.titleBarHeight = 34.0;
        metrics.topBarHeight = 38.0;
        metrics.sidebarSearchHeight = 44.0;
        metrics.tabStripHeight = 36.0;
        metrics.externalNoticeHeight = 40.0;
        metrics.statusBarHeight = 28.0;
    }
    metrics.splitterThickness = settings_.density == settings::UiDensity::Compact ? 5.0 : 6.0;
    static_cast<void>(shell_.setLayoutMetrics(metrics));
}

void WxMainFrame::updateOuterFrameInset() {
    auto metrics = shell_.layoutMetrics();
    const double desired = (IsMaximized() || IsFullScreen()) ? 0.0 : 3.0;
    if (std::abs(metrics.outerFrameInset - desired) < 0.001) {
        return;
    }
    metrics.outerFrameInset = desired;
    static_cast<void>(shell_.setLayoutMetrics(metrics));
}

void WxMainFrame::updateResizeZones() noexcept {
#ifdef __WXGTK__
    if (resizeTop_ == nullptr || resizeBottom_ == nullptr ||
        resizeLeft_ == nullptr || resizeRight_ == nullptr) {
        return;
    }

    const auto client = GetClientSize();
    const int width = std::max(0, client.GetWidth());
    const int height = std::max(0, client.GetHeight());
    const int inset = (IsMaximized() || IsFullScreen())
                          ? 0
                          : std::max(1, toPixel(shell_.layoutMetrics().outerFrameInset));
    const int sideHeight = std::max(0, height - inset * 2);

    const bool topChanged = setPixelBounds(resizeTop_, 0, 0, width, inset);
    const bool bottomChanged = setPixelBounds(resizeBottom_, 0, std::max(0, height - inset), width, inset);
    const bool leftChanged = setPixelBounds(resizeLeft_, 0, inset, inset, sideHeight);
    const bool rightChanged = setPixelBounds(resizeRight_, std::max(0, width - inset), inset, inset, sideHeight);

    const bool visible = inset > 0 && width > 0 && height > 0;
    const auto updateZone = [visible](wxWindow* zone, const bool geometryChanged) {
        const bool visibilityChanged = zone->IsShown() != visible;
        if (visibilityChanged) zone->Show(visible);
        if (visible && (geometryChanged || visibilityChanged)) zone->Raise();
    };
    updateZone(resizeTop_, topChanged);
    updateZone(resizeBottom_, bottomChanged);
    updateZone(resizeLeft_, leftChanged);
    updateZone(resizeRight_, rightChanged);
#endif
}

void WxMainFrame::refreshEditorAppearance() {
    updateThemeControls();
    if (runtime_ != nullptr) {
        runtime_->refreshHostAppearance();
    }
    shell_.requestRefresh();
    scheduleSynchronize();
}

void WxMainFrame::applyTheme(const settings::ThemePreference preference) {
    settings_.theme = preference;
    theme_ = themeFor(preference, settings_.accent);

#if wxCHECK_VERSION(3, 3, 0)
    if (wxTheApp != nullptr) {
        switch (preference) {
        case settings::ThemePreference::System:
            static_cast<void>(wxTheApp->SetAppearance(wxAppBase::Appearance::System));
            break;
        case settings::ThemePreference::Light:
            static_cast<void>(wxTheApp->SetAppearance(wxAppBase::Appearance::Light));
            break;
        case settings::ThemePreference::Dark:
            static_cast<void>(wxTheApp->SetAppearance(wxAppBase::Appearance::Dark));
            break;
        }
    }
#endif

    refreshEditorAppearance();
    persistSettings();
}

void WxMainFrame::applyAccent(const settings::AccentPreference preference) {
    settings_.accent = preference;
    theme_ = themeFor(settings_.theme, settings_.accent);
    refreshEditorAppearance();
    persistSettings();
}

void WxMainFrame::applyDensity(const settings::UiDensity density) {
    settings_.density = density;
    if (titleBar_ != nullptr) {
        titleBar_->setCompact(density == settings::UiDensity::Compact);
    }
    applyLayoutDensity();
    refreshEditorAppearance();
    persistSettings();
}

std::error_code WxMainFrame::showSettingsDialog() {
    wxDialog dialog(this, wxID_ANY, "Settings", wxDefaultPosition, wxDefaultSize,
                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
    dialog.SetName("dialog.settings");
#endif

#ifdef __WXGTK__
    configureGtkThemedDialog(dialog, theme_, "Settings");
#endif

    dialog.SetBackgroundColour(theme_.surfaceRaised);
    dialog.SetForegroundColour(theme_.text);

    auto* rootSizer = new wxBoxSizer(wxVERTICAL);
    const auto makeSection = [this, &dialog, rootSizer](const wxString& title) {
        auto* heading = new wxStaticText(&dialog, wxID_ANY, title);
        auto headingFont = heading->GetFont();
        headingFont.MakeBold();
        heading->SetFont(headingFont);
        heading->SetForegroundColour(theme_.text);
        heading->SetBackgroundColour(theme_.surfaceRaised);
        rootSizer->Add(heading, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

        auto* grid = new wxFlexGridSizer(2, 4, 12);
        grid->AddGrowableCol(1, 1);
        rootSizer->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 12);
        return grid;
    };
    const auto addRow = [this, &dialog](wxFlexGridSizer* grid,
                                        const wxString& label,
                                        wxWindow* control) {
        auto* caption = new wxStaticText(&dialog, wxID_ANY, label);
        caption->SetForegroundColour(theme_.mutedText);
        caption->SetBackgroundColour(theme_.surfaceRaised);
        grid->Add(caption, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 2);
        grid->Add(control, 1, wxEXPAND);
    };
    const auto styleControl = [this](wxWindow* control, const bool inputSurface = false) {
        if (control == nullptr) {
            return;
        }
        control->SetForegroundColour(theme_.text);
        control->SetBackgroundColour(inputSurface ? theme_.surface : theme_.surfaceRaised);
    };

    auto* startupGrid = makeSection("Startup & Recovery");
    auto* restoreSession = new wxCheckBox(&dialog, wxID_ANY, "Restore previous session on startup");
    restoreSession->SetValue(settings_.restorePreviousSession);
    addRow(startupGrid, "Startup", restoreSession);

    auto* recoveryEnabled = new wxCheckBox(&dialog, wxID_ANY, "Keep crash-recovery checkpoints");
    recoveryEnabled->SetValue(settings_.autoSave.recoveryEnabled);
    addRow(startupGrid, "Recovery", recoveryEnabled);

    auto* recoveryDelay = new wxTextCtrl(
        &dialog, wxID_ANY, fromUtf8(std::to_string(settings_.autoSave.recoveryDelay.count())));
    recoveryDelay->SetMinSize(wxSize(150, -1));
    addRow(startupGrid, "Recovery delay (ms)", recoveryDelay);

    auto* saveRealFiles = new wxCheckBox(&dialog, wxID_ANY, "Autosave real files");
    saveRealFiles->SetValue(settings_.autoSave.saveRealFiles);
    addRow(startupGrid, "Autosave", saveRealFiles);

    auto* saveDelay = new wxTextCtrl(
        &dialog, wxID_ANY, fromUtf8(std::to_string(settings_.autoSave.saveDelay.count())));
    addRow(startupGrid, "Autosave delay (ms)", saveDelay);

    auto* saveOnFocusLoss = new wxCheckBox(&dialog, wxID_ANY, "Save on focus loss");
    saveOnFocusLoss->SetValue(settings_.autoSave.saveOnFocusLoss);
    addRow(startupGrid, "Focus loss", saveOnFocusLoss);

    constexpr std::uintmax_t mebibyte = 1024ULL * 1024ULL;
    constexpr std::uintmax_t kibibyte = 1024ULL;
    const auto viewerThresholdMiB = std::max<std::uintmax_t>(
        1U, settings_.inspectOptions.viewerSizeThreshold / mebibyte +
            (settings_.inspectOptions.viewerSizeThreshold % mebibyte != 0U ? 1U : 0U));
    const auto longLineKiB = std::max<std::uintmax_t>(
        1U, static_cast<std::uintmax_t>(settings_.inspectOptions.longLineThreshold) / kibibyte +
            (static_cast<std::uintmax_t>(settings_.inspectOptions.longLineThreshold) % kibibyte != 0U
                 ? 1U
                 : 0U));

    auto* largeFileGrid = makeSection("Large Files");
    auto* viewerThreshold = new wxTextCtrl(
        &dialog, wxID_ANY, fromUtf8(std::to_string(viewerThresholdMiB)));
    addRow(largeFileGrid, "Auto View Mode threshold (MiB)", viewerThreshold);

    auto* longLineThreshold = new wxTextCtrl(
        &dialog, wxID_ANY, fromUtf8(std::to_string(longLineKiB)));
    addRow(largeFileGrid, "Long-line View threshold (KiB)", longLineThreshold);

    auto* performance = new wxChoice(&dialog, wxID_ANY);
    performance->Append("Automatic");
    performance->Append("Fast");
    performance->Append("Memory Saver");
    performance->SetSelection(static_cast<int>(settings_.viewerPerformance));
    addRow(largeFileGrid, "Strategy", performance);

    auto* interfaceGrid = makeSection("Interface");
    auto* sidebarVisible = new wxCheckBox(&dialog, wxID_ANY, "Show sidebar / allow indexing when used");
    sidebarVisible->SetValue(settings_.sidebarVisible);
    addRow(interfaceGrid, "Sidebar", sidebarVisible);

    auto* tabsEnabled = new wxCheckBox(&dialog, wxID_ANY, "Show tabs");
    tabsEnabled->SetValue(settings_.tabsEnabled);
    addRow(interfaceGrid, "Tabs", tabsEnabled);

    auto* editingGrid = makeSection("Editing");
    auto* wordWrap = new wxCheckBox(&dialog, wxID_ANY, "Wrap new editor views");
    wordWrap->SetValue(settings_.wordWrap);
    addRow(editingGrid, "Default wrap", wordWrap);

    auto* lineNumbers = new wxCheckBox(&dialog, wxID_ANY, "Show line numbers in new views");
    lineNumbers->SetValue(settings_.showLineNumbers);
    addRow(editingGrid, "Line numbers", lineNumbers);

    auto* links = new wxCheckBox(&dialog, wxID_ANY, "Enable Ctrl+Click for web/email links");
    links->SetValue(settings_.highlightUrls);
    addRow(editingGrid, "Links", links);

    auto* fontFamily = new wxTextCtrl(&dialog, wxID_ANY, fromUtf8(settings_.fontFamily));
    fontFamily->SetHint("System default when empty");
    addRow(editingGrid, "Default font family", fontFamily);

    auto* fontSize = new wxTextCtrl(
        &dialog, wxID_ANY, fromUtf8(formatPointSizeForUi(settings_.fontPointSize)));
    addRow(editingGrid, "Default font size (pt)", fontSize);

    auto* appearanceGrid = makeSection("Appearance");
    auto* themeChoice = new wxChoice(&dialog, wxID_ANY);
    themeChoice->Append("System");
    themeChoice->Append("Light");
    themeChoice->Append("Dark");
    themeChoice->SetSelection(static_cast<int>(settings_.theme));
    addRow(appearanceGrid, "Theme", themeChoice);

    auto* accentChoice = new wxChoice(&dialog, wxID_ANY);
    accentChoice->Append("Violet");
    accentChoice->Append("Blue");
    accentChoice->Append("Teal");
    accentChoice->Append("Rose");
    accentChoice->Append("Amber");
    accentChoice->SetSelection(static_cast<int>(settings_.accent));
    addRow(appearanceGrid, "Accent", accentChoice);

    auto* densityChoice = new wxChoice(&dialog, wxID_ANY);
    densityChoice->Append("Compact");
    densityChoice->Append("Comfortable");
    densityChoice->SetSelection(static_cast<int>(settings_.density));
    addRow(appearanceGrid, "Density", densityChoice);

    for (auto* control : {static_cast<wxWindow*>(restoreSession),
                          static_cast<wxWindow*>(recoveryEnabled),
                          static_cast<wxWindow*>(saveRealFiles),
                          static_cast<wxWindow*>(saveOnFocusLoss),
                          static_cast<wxWindow*>(sidebarVisible),
                          static_cast<wxWindow*>(tabsEnabled),
                          static_cast<wxWindow*>(wordWrap),
                          static_cast<wxWindow*>(lineNumbers),
                          static_cast<wxWindow*>(links)}) {
        styleControl(control, false);
    }
    for (auto* control : {static_cast<wxWindow*>(recoveryDelay),
                          static_cast<wxWindow*>(saveDelay),
                          static_cast<wxWindow*>(viewerThreshold),
                          static_cast<wxWindow*>(longLineThreshold),
                          static_cast<wxWindow*>(performance),
                          static_cast<wxWindow*>(fontFamily),
                          static_cast<wxWindow*>(fontSize),
                          static_cast<wxWindow*>(themeChoice),
                          static_cast<wxWindow*>(accentChoice),
                          static_cast<wxWindow*>(densityChoice)}) {
        styleControl(control, true);
    }

#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
    restoreSession->SetName("settings.restore_session");
    recoveryEnabled->SetName("settings.recovery_enabled");
    recoveryDelay->SetName("settings.recovery_delay");
    saveRealFiles->SetName("settings.autosave_files");
    saveDelay->SetName("settings.autosave_delay");
    saveOnFocusLoss->SetName("settings.save_focus_loss");
    viewerThreshold->SetName("settings.viewer_threshold");
    longLineThreshold->SetName("settings.long_line_threshold");
    performance->SetName("settings.viewer_performance");
    sidebarVisible->SetName("settings.sidebar_visible");
    tabsEnabled->SetName("settings.tabs_enabled");
    wordWrap->SetName("settings.word_wrap");
    lineNumbers->SetName("settings.line_numbers");
    links->SetName("settings.links");
    themeChoice->SetName("settings.theme");
    accentChoice->SetName("settings.accent");
    densityChoice->SetName("settings.density");
    fontFamily->SetName("settings.font_family");
    fontSize->SetName("settings.font_size");
#endif

    if (auto* buttons = dialog.CreateButtonSizer(wxOK | wxCANCEL); buttons != nullptr) {
        rootSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM | wxTOP, 14);
    }
    auto* settingsOk = dialog.FindWindow(wxID_OK);
    auto* settingsCancel = dialog.FindWindow(wxID_CANCEL);
    styleControl(settingsOk, true);
    styleControl(settingsCancel, false);
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
    if (settingsOk != nullptr) settingsOk->SetName("settings.ok");
    if (settingsCancel != nullptr) settingsCancel->SetName("settings.cancel");
#endif
    dialog.SetSizerAndFit(rootSizer);
    const auto fitted = dialog.GetSize();
    dialog.SetMinSize(wxSize(std::max(620, fitted.x), fitted.y));
    dialog.SetSize(dialog.GetMinSize());

    const auto parseUnsigned = [](wxTextCtrl* control, unsigned long long& value) {
        return control != nullptr && control->GetValue().ToULongLong(&value, 10);
    };

    while (dialog.ShowModal() == wxID_OK) {
        settings::AppSettings next = settings_;
        next.restorePreviousSession = restoreSession->GetValue();
        next.autoSave.recoveryEnabled = recoveryEnabled->GetValue();
        next.autoSave.saveRealFiles = saveRealFiles->GetValue();
        next.autoSave.saveOnFocusLoss = saveOnFocusLoss->GetValue();
        next.sidebarVisible = sidebarVisible->GetValue();
        next.tabsEnabled = tabsEnabled->GetValue();
        next.wordWrap = wordWrap->GetValue();
        next.showLineNumbers = lineNumbers->GetValue();
        next.highlightUrls = links->GetValue();

        unsigned long long recoveryMs = 0U;
        unsigned long long saveMs = 0U;
        unsigned long long viewerMiB = 0U;
        unsigned long long longLineKiBValue = 0U;
        double pointSize = 0.0;
        if (!parseUnsigned(recoveryDelay, recoveryMs) ||
            !parseUnsigned(saveDelay, saveMs) ||
            !parseUnsigned(viewerThreshold, viewerMiB) ||
            !parseUnsigned(longLineThreshold, longLineKiBValue) ||
            !fontSize->GetValue().ToDouble(&pointSize) ||
            viewerMiB == 0U || longLineKiBValue == 0U ||
            recoveryMs > static_cast<unsigned long long>(
                             std::numeric_limits<std::chrono::milliseconds::rep>::max()) ||
            saveMs > static_cast<unsigned long long>(
                         std::numeric_limits<std::chrono::milliseconds::rep>::max()) ||
            viewerMiB > static_cast<unsigned long long>(
                            std::numeric_limits<std::uintmax_t>::max() / mebibyte) ||
            longLineKiBValue > static_cast<unsigned long long>(
                                   std::numeric_limits<std::size_t>::max() / kibibyte)) {
            wxMessageBox("One or more numeric settings are invalid or out of range.",
                         "Invalid Settings", wxOK | wxICON_WARNING, &dialog);
            continue;
        }

        next.autoSave.recoveryDelay =
            std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(recoveryMs)};
        next.autoSave.saveDelay =
            std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(saveMs)};
        next.inspectOptions.viewerSizeThreshold =
            static_cast<std::uintmax_t>(viewerMiB) * mebibyte;
        next.inspectOptions.longLineThreshold =
            static_cast<std::size_t>(longLineKiBValue) * static_cast<std::size_t>(kibibyte);
        next.viewerPerformance = static_cast<viewer::PerformanceProfile>(performance->GetSelection());
        next.theme = static_cast<settings::ThemePreference>(themeChoice->GetSelection());
        next.accent = static_cast<settings::AccentPreference>(accentChoice->GetSelection());
        next.density = static_cast<settings::UiDensity>(densityChoice->GetSelection());
        next.fontFamily = toUtf8(fontFamily->GetValue());
        next.fontPointSize = pointSize;

        if (performance->GetSelection() == wxNOT_FOUND ||
            themeChoice->GetSelection() == wxNOT_FOUND ||
            accentChoice->GetSelection() == wxNOT_FOUND ||
            densityChoice->GetSelection() == wxNOT_FOUND ||
            !settings::validate(next)) {
            wxMessageBox("The selected settings are not valid.",
                         "Invalid Settings", wxOK | wxICON_WARNING, &dialog);
            continue;
        }

        const auto previous = settings_;
        settings_ = next;

        if (previous.sidebarVisible != settings_.sidebarVisible) {
            shell_.setSidebarVisible(settings_.sidebarVisible);
            if (!settings_.sidebarVisible) {
                stopSidebarSearch();
                stopSidebarIndex();
                fileSearch_.clear();
                sidebarIndexedFiles_ = 0U;
                sidebarIndexAvailable_ = false;
            } else if (!sidebarRoot_.empty()) {
                std::error_code rootError;
                sidebarRootUnavailable_ = !std::filesystem::is_directory(sidebarRoot_, rootError) ||
                                          static_cast<bool>(rootError);
                resetSidebarTree();
                updateSidebarHint();
            }
        }
        if (previous.tabsEnabled != settings_.tabsEnabled) {
            tabsFingerprint_.reset();
        }

        theme_ = themeFor(settings_.theme, settings_.accent);
#if wxCHECK_VERSION(3, 3, 0)
        if (wxTheApp != nullptr) {
            switch (settings_.theme) {
            case settings::ThemePreference::System:
                static_cast<void>(wxTheApp->SetAppearance(wxAppBase::Appearance::System));
                break;
            case settings::ThemePreference::Light:
                static_cast<void>(wxTheApp->SetAppearance(wxAppBase::Appearance::Light));
                break;
            case settings::ThemePreference::Dark:
                static_cast<void>(wxTheApp->SetAppearance(wxAppBase::Appearance::Dark));
                break;
            }
        }
#endif
        if (titleBar_ != nullptr) {
            titleBar_->setCompact(settings_.density == settings::UiDensity::Compact);
        }
        applyLayoutDensity();

        workspace_.forEachView([this](const workspace::ViewState& view) {
            const auto* persisted = workspace_.view(view.id);
            const auto* runtimeState = presentation_.viewRuntime(view.id);
            if (persisted == nullptr || runtimeState == nullptr) return;
            auto runtime = *runtimeState;
            if (!persisted->wordWrapOverride) {
                runtime.wordWrap = settings_.wordWrap;
            }
            if (!persisted->lineNumbersOverride) {
                runtime.lineNumbersVisible = settings_.showLineNumbers;
            }
            if (!persisted->fontFamilyOverride) {
                runtime.fontFamily = settings_.fontFamily;
            }
            if (!persisted->fontPointSizeOverride) {
                runtime.fontPointSize = settings_.fontPointSize;
            }
            runtime.viewerPerformance = settings_.viewerPerformance;
            runtime.resolvedViewerPerformance = settings_.viewerPerformance;
            static_cast<void>(presentation_.setViewRuntime(view.id, runtime));
        });

        refreshEditorAppearance();
        shell_.requestRefresh();
        scheduleSynchronize();
        return lifecycle_.saveSettings();
    }

    return {};
}

void WxMainFrame::chooseEditorFont() {
    const auto refreshed = fontManager_.refresh();
    if (!refreshed) {
        showError("Could not enumerate system fonts", refreshed.error);
        return;
    }

    wxArrayString choices;
    choices.Add("System Default");
    int selection = 0;
    int index = 1;
    for (const auto& family : fontManager_.families()) {
        choices.Add(fromUtf8(family));
        if (!settings_.fontFamily.empty() && family == settings_.fontFamily) {
            selection = index;
        }
        ++index;
    }

    wxSingleChoiceDialog dialog(this, "Choose the editor font", "Editor Font", choices);
    dialog.SetSelection(selection);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    const auto chosen = dialog.GetSelection();
    settings_.fontFamily = chosen <= 0 ? std::string{} : toUtf8(dialog.GetStringSelection());
    workspace_.forEachView([this](const workspace::ViewState& view) {
        const auto* persisted = workspace_.view(view.id);
        const auto* runtimeState = presentation_.viewRuntime(view.id);
        if (persisted == nullptr || runtimeState == nullptr || persisted->fontFamilyOverride) return;
        auto next = *runtimeState;
        next.fontFamily = settings_.fontFamily;
        static_cast<void>(presentation_.setViewRuntime(view.id, next));
    });
    refreshEditorAppearance();
    persistSettings();
}

void WxMainFrame::setEditorFontSize(const double pointSize) {
    settings_.fontPointSize = pointSize;
    workspace_.forEachView([this](const workspace::ViewState& view) {
        const auto* persisted = workspace_.view(view.id);
        const auto* runtimeState = presentation_.viewRuntime(view.id);
        if (persisted == nullptr || runtimeState == nullptr || persisted->fontPointSizeOverride) return;
        auto next = *runtimeState;
        next.fontPointSize = settings_.fontPointSize;
        static_cast<void>(presentation_.setViewRuntime(view.id, next));
    });
    refreshEditorAppearance();
    persistSettings();
}

void WxMainFrame::showTabContextMenu(wxWindow& anchor, const workspace::ViewId view) {
    const auto* runtimeState = presentation_.viewRuntime(view);
    const auto* viewState = workspace_.view(view);
    if (runtimeState == nullptr || viewState == nullptr) {
        return;
    }

    wxMenu menu;
    menu.Append(UiTabClose, "Close Tab");
    menu.AppendSeparator();
    auto* wrap = menu.AppendCheckItem(UiTabWordWrap, "Word Wrap");
    wrap->Check(runtimeState->wordWrap);
    auto* lineNumbers = menu.AppendCheckItem(UiTabLineNumbers, "Line Numbers");
    lineNumbers->Check(runtimeState->lineNumbersVisible);

    auto* fontMenu = new wxMenu();
    wxString fontLabel{"Choose Font..."};
    if (!runtimeState->fontFamily.empty()) {
        fontLabel += "  [" + fromUtf8(runtimeState->fontFamily) + "]";
    }
    fontMenu->Append(UiTabChooseFont, fontLabel);
    auto* fontSizeMenu = new wxMenu();
    const auto addSize = [runtimeState, fontSizeMenu](const int id, const int size) {
        auto* item = fontSizeMenu->AppendRadioItem(id, wxString::Format("%d pt", size));
        item->Check(runtimeState->fontPointSize == static_cast<double>(size));
    };
    addSize(UiTabFontSize10, 10);
    addSize(UiTabFontSize11, 11);
    addSize(UiTabFontSize12, 12);
    addSize(UiTabFontSize13, 13);
    addSize(UiTabFontSize14, 14);
    addSize(UiTabFontSize16, 16);
    addSize(UiTabFontSize18, 18);
    addSize(UiTabFontSize20, 20);
    fontMenu->AppendSubMenu(fontSizeMenu, "Size");
    menu.AppendSubMenu(fontMenu, "Tab Font");

    menu.AppendSeparator();
    auto* reset = menu.Append(UiTabResetAppearance, "Reset Tab Appearance");
    reset->Enable(viewState->wordWrapOverride.has_value() ||
                  viewState->lineNumbersOverride.has_value() ||
                  viewState->fontFamilyOverride.has_value() ||
                  viewState->fontPointSizeOverride.has_value());

    const auto selected = anchor.GetPopupMenuSelectionFromUser(menu);
    if (selected == wxID_NONE) {
        return;
    }

    static_cast<void>(shell_.activateView(view));
    switch (selected) {
    case UiTabClose:
        static_cast<void>(closeView(view));
        break;
    case UiTabWordWrap:
        static_cast<void>(toggleWordWrap());
        break;
    case UiTabLineNumbers:
        static_cast<void>(toggleLineNumbers());
        break;
    case UiTabChooseFont:
        chooseEditorFontForView(view);
        break;
    case UiTabFontSize10:
        setEditorFontSizeForView(view, 10.0);
        break;
    case UiTabFontSize11:
        setEditorFontSizeForView(view, 11.0);
        break;
    case UiTabFontSize12:
        setEditorFontSizeForView(view, 12.0);
        break;
    case UiTabFontSize13:
        setEditorFontSizeForView(view, 13.0);
        break;
    case UiTabFontSize14:
        setEditorFontSizeForView(view, 14.0);
        break;
    case UiTabFontSize16:
        setEditorFontSizeForView(view, 16.0);
        break;
    case UiTabFontSize18:
        setEditorFontSizeForView(view, 18.0);
        break;
    case UiTabFontSize20:
        setEditorFontSizeForView(view, 20.0);
        break;
    case UiTabResetAppearance:
        resetEditorAppearanceForView(view);
        break;
    default:
        break;
    }
}

void WxMainFrame::showEditorContextMenu(wxWindow& anchor, const workspace::ViewId view) {
    anchor.SetFocus();
    const auto pane = workspace_.paneContaining(view);
    if (pane) {
        static_cast<void>(workspace_.setActiveView(*pane, view));
        static_cast<void>(workspace_.setActivePane(*pane));
    }
    synchronizeNow();

    wxMenu menu;
    appendCommand(menu, UiCut, "Cut\tCtrl+X", app::CommandId::Cut);
    appendCommand(menu, UiCopy, "Copy\tCtrl+C", app::CommandId::Copy);
    appendCommand(menu, UiPaste, "Paste\tCtrl+V", app::CommandId::Paste);
    menu.AppendSeparator();

    auto* appearanceMenu = new wxMenu();
    appendCommand(*appearanceMenu, UiTextColorChoose, "Text Color...\tCtrl+Shift+C",
                  app::CommandId::TextColorChoose);
    appendCommand(*appearanceMenu, UiTextColorClear, "Clear Text Color",
                  app::CommandId::TextColorClear);
    appearanceMenu->AppendSeparator();
    appendCommand(*appearanceMenu, UiSelectionFontFamilyChoose, "Font Family...",
                  app::CommandId::SelectionFontFamilyChoose);
    appendCommand(*appearanceMenu, UiSelectionFontFamilyClear, "Inherit Document Font",
                  app::CommandId::SelectionFontFamilyClear);
    appendCommand(*appearanceMenu, UiSelectionFontSizeChoose, "Font Size...",
                  app::CommandId::SelectionFontSizeChoose);
    appendCommand(*appearanceMenu, UiSelectionFontSizeClear, "Inherit Document Size",
                  app::CommandId::SelectionFontSizeClear);
    appearanceMenu->AppendSeparator();
    appendCommand(*appearanceMenu, UiSelectionSpoilerSet, "Censor / Spoiler",
                  app::CommandId::SelectionSpoilerSet);
    appendCommand(*appearanceMenu, UiSelectionSpoilerClear, "Remove Censor / Spoiler",
                  app::CommandId::SelectionSpoilerClear);
    appearanceMenu->AppendSeparator();
    appendCommand(*appearanceMenu, UiSelectionAppearanceReset, "Reset Selection Appearance",
                  app::CommandId::SelectionAppearanceReset);
    menu.AppendSubMenu(appearanceMenu, "Selection Appearance");
    menu.AppendSeparator();
    appendCommand(menu, UiSelectAll, "Select All\tCtrl+A", app::CommandId::SelectAll);
    anchor.PopupMenu(&menu);
}

void WxMainFrame::activateDetectedLink(const workspace::ViewId view,
                                        const core::LinkSpan& link) {
    static_cast<void>(shell_.activateView(view));
    if (!wxLaunchDefaultBrowser(fromUtf8(link.target), wxBROWSER_NEW_WINDOW)) {
        wxMessageBox("No default handler could open this link.",
                     "Open Link", wxOK | wxICON_WARNING, this);
    }
}

void WxMainFrame::chooseEditorFontForView(const workspace::ViewId view) {
    auto* viewState = workspace_.view(view);
    const auto* runtimeState = presentation_.viewRuntime(view);
    if (viewState == nullptr || runtimeState == nullptr) {
        return;
    }

    const auto refreshed = fontManager_.refresh();
    if (!refreshed) {
        showError("Could not enumerate system fonts", refreshed.error);
        return;
    }

    wxArrayString choices;
    choices.Add("System Default");
    int selection = 0;
    int index = 1;
    for (const auto& family : fontManager_.families()) {
        choices.Add(fromUtf8(family));
        if (!runtimeState->fontFamily.empty() && family == runtimeState->fontFamily) {
            selection = index;
        }
        ++index;
    }

    wxSingleChoiceDialog dialog(this, "Choose the font for this tab", "Tab Font", choices);
    dialog.SetSelection(selection);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }

    const auto chosen = dialog.GetSelection();
    const auto family = chosen <= 0 ? std::string{} : toUtf8(dialog.GetStringSelection());
    viewState->fontFamilyOverride = family;
    auto next = *runtimeState;
    next.fontFamily = family;
    static_cast<void>(presentation_.setViewRuntime(view, next));
    static_cast<void>(lifecycle_.saveSession());
    shell_.requestRefresh();
}

void WxMainFrame::setEditorFontSizeForView(const workspace::ViewId view,
                                           const double pointSize) {
    auto* viewState = workspace_.view(view);
    const auto* runtimeState = presentation_.viewRuntime(view);
    if (viewState == nullptr || runtimeState == nullptr) {
        return;
    }
    viewState->fontPointSizeOverride = pointSize;
    auto next = *runtimeState;
    next.fontPointSize = pointSize;
    static_cast<void>(presentation_.setViewRuntime(view, next));
    static_cast<void>(lifecycle_.saveSession());
    shell_.requestRefresh();
}

void WxMainFrame::resetEditorAppearanceForView(const workspace::ViewId view) {
    auto* viewState = workspace_.view(view);
    const auto* runtimeState = presentation_.viewRuntime(view);
    if (viewState == nullptr || runtimeState == nullptr) {
        return;
    }

    viewState->wordWrapOverride.reset();
    viewState->lineNumbersOverride.reset();
    viewState->fontFamilyOverride.reset();
    viewState->fontPointSizeOverride.reset();
    auto next = *runtimeState;
    next.wordWrap = settings_.wordWrap;
    next.lineNumbersVisible = settings_.showLineNumbers;
    next.fontFamily = settings_.fontFamily;
    next.fontPointSize = settings_.fontPointSize;
    static_cast<void>(presentation_.setViewRuntime(view, next));
    static_cast<void>(lifecycle_.saveSession());
    shell_.requestRefresh();
}

void WxMainFrame::showFileMenu(wxWindow& anchor) {
    wxMenu menu;
    appendCommand(menu, UiNew, "New\tCtrl+N", app::CommandId::NewDocument);
    appendCommand(menu, UiOpen, "Open...\tCtrl+O", app::CommandId::OpenFile);
    menu.AppendSeparator();
    appendCommand(menu, UiSidebarChooseFolder, "Choose Folder...",
                  app::CommandId::SidebarChooseFolder);
    appendCommand(menu, UiSidebarReindexFolder, "Refresh Search Index\tF5",
                  app::CommandId::SidebarReindexFolder);
    appendCommand(menu, UiSidebarClearFolder, "Clear Folder",
                  app::CommandId::SidebarClearFolder);
    menu.AppendSeparator();
    appendCommand(menu, UiSave, "Save\tCtrl+S", app::CommandId::Save);
    appendCommand(menu, UiSaveAs, "Save As...\tCtrl+Shift+S", app::CommandId::SaveAs);
    appendCommand(menu, UiSaveCopy, "Save Copy...", app::CommandId::SaveCopy);
    appendCommand(menu, UiReload, "Reload from Disk", app::CommandId::Reload);

    auto* reopenEncodingMenu = new wxMenu();
    appendCommand(*reopenEncodingMenu, UiReopenAsUtf8, "UTF-8", app::CommandId::ReopenAsUtf8);
    appendCommand(*reopenEncodingMenu, UiReopenAsUtf16LE, "UTF-16 LE", app::CommandId::ReopenAsUtf16LE);
    appendCommand(*reopenEncodingMenu, UiReopenAsUtf16BE, "UTF-16 BE", app::CommandId::ReopenAsUtf16BE);
    appendCommand(*reopenEncodingMenu, UiReopenAsUtf32LE, "UTF-32 LE", app::CommandId::ReopenAsUtf32LE);
    appendCommand(*reopenEncodingMenu, UiReopenAsUtf32BE, "UTF-32 BE", app::CommandId::ReopenAsUtf32BE);
    reopenEncodingMenu->AppendSeparator();
    appendCommand(*reopenEncodingMenu, UiReopenAsWindows1252, "Windows-1252",
                  app::CommandId::ReopenAsWindows1252);
    appendCommand(*reopenEncodingMenu, UiReopenAsWindows1254, "Windows-1254 (Turkish)",
                  app::CommandId::ReopenAsWindows1254);
    menu.AppendSubMenu(reopenEncodingMenu, "Reopen As Encoding");

    auto* encodingMenu = new wxMenu();
    appendRadioCommand(*encodingMenu, UiEncodingUtf8, "UTF-8", app::CommandId::EncodingUtf8);
    appendRadioCommand(*encodingMenu, UiEncodingUtf16LE, "UTF-16 LE", app::CommandId::EncodingUtf16LE);
    appendRadioCommand(*encodingMenu, UiEncodingUtf16BE, "UTF-16 BE", app::CommandId::EncodingUtf16BE);
    appendRadioCommand(*encodingMenu, UiEncodingUtf32LE, "UTF-32 LE", app::CommandId::EncodingUtf32LE);
    appendRadioCommand(*encodingMenu, UiEncodingUtf32BE, "UTF-32 BE", app::CommandId::EncodingUtf32BE);
    encodingMenu->AppendSeparator();
    appendRadioCommand(*encodingMenu, UiEncodingWindows1252, "Windows-1252",
                       app::CommandId::EncodingWindows1252);
    appendRadioCommand(*encodingMenu, UiEncodingWindows1254, "Windows-1254 (Turkish)",
                       app::CommandId::EncodingWindows1254);
    menu.AppendSubMenu(encodingMenu, "Save Encoding");
    appendCommand(menu, UiToggleBom, "Write Byte Order Mark (BOM)",
                  app::CommandId::ToggleBom, true);

    auto* lineEndingMenu = new wxMenu();
    appendRadioCommand(*lineEndingMenu, UiLineEndingLF, "LF (Unix / macOS)",
                       app::CommandId::LineEndingLF);
    appendRadioCommand(*lineEndingMenu, UiLineEndingCRLF, "CRLF (Windows)",
                       app::CommandId::LineEndingCRLF);
    appendRadioCommand(*lineEndingMenu, UiLineEndingCR, "CR (Classic Mac)",
                       app::CommandId::LineEndingCR);
    menu.AppendSubMenu(lineEndingMenu, "Line Endings");

    auto* recentMenu = new wxMenu();
    if (lifecycle_.recentFiles().pruneMissing() != 0U) {
        static_cast<void>(lifecycle_.saveRecentFiles());
    }
    const auto recentEntries = lifecycle_.recentFiles().entries();
    if (recentEntries.empty()) {
        recentMenu->Append(UiRecentBase + 99, "(Empty)")->Enable(false);
    } else {
        const auto count = std::min<std::size_t>(recentEntries.size(), 12U);
        for (std::size_t index = 0U; index < count; ++index) {
            const auto path = recentEntries[index].path;
            const int id = UiRecentBase + static_cast<int>(index);
            recentMenu->Append(id, fromUtf8(pathUtf8(path)));
            recentMenu->Bind(wxEVT_MENU, [this, path](wxCommandEvent&) {
                static_cast<void>(openPath(path));
            }, id);
        }
    }
    menu.AppendSubMenu(recentMenu, "Recent Files");
    menu.AppendSeparator();
    appendCommand(menu, UiCloseView, "Close Tab\tCtrl+W", app::CommandId::CloseView);
    menu.AppendSeparator();
    menu.Append(UiQuit, "Exit");
    anchor.PopupMenu(&menu, wxPoint(0, anchor.GetClientSize().y));
}

void WxMainFrame::showEditMenu(wxWindow& anchor) {
    wxMenu menu;
    appendCommand(menu, UiUndo, "Undo\tCtrl+Z", app::CommandId::Undo);
    appendCommand(menu, UiRedo, "Redo\tCtrl+Y", app::CommandId::Redo);
    menu.AppendSeparator();
    appendCommand(menu, UiCut, "Cut\tCtrl+X", app::CommandId::Cut);
    appendCommand(menu, UiCopy, "Copy\tCtrl+C", app::CommandId::Copy);
    appendCommand(menu, UiPaste, "Paste\tCtrl+V", app::CommandId::Paste);
    appendCommand(menu, UiSelectAll, "Select All\tCtrl+A", app::CommandId::SelectAll);
    menu.AppendSeparator();
    appendCommand(menu, UiFind, "Find...\tCtrl+F", app::CommandId::Find);
    appendCommand(menu, UiFindNext, "Find Next\tF3", app::CommandId::FindNext);
    appendCommand(menu, UiFindPrevious, "Find Previous\tShift+F3",
                  app::CommandId::FindPrevious);
    appendCommand(menu, UiReplace, "Replace...\tCtrl+H", app::CommandId::Replace);
    appendCommand(menu, UiGoToLine, "Go To Line...\tCtrl+G", app::CommandId::GoToLine);
    anchor.PopupMenu(&menu, wxPoint(0, anchor.GetClientSize().y));
}

void WxMainFrame::showFormatMenu(wxWindow& anchor) {
    wxMenu menu;
    appendCommand(menu, UiFormatValidate, "Validate JSON", app::CommandId::FormatValidate);
    appendCommand(menu, UiFormatPretty, "Pretty JSON", app::CommandId::FormatPretty);
    appendCommand(menu, UiFormatMinify, "Minify JSON", app::CommandId::FormatMinify);
    menu.AppendSeparator();

    auto* appearanceMenu = new wxMenu();
    appendCommand(*appearanceMenu, UiTextColorChoose, "Text Color...\tCtrl+Shift+C",
                  app::CommandId::TextColorChoose);
    appendCommand(*appearanceMenu, UiTextColorClear, "Clear Text Color",
                  app::CommandId::TextColorClear);
    appearanceMenu->AppendSeparator();
    appendCommand(*appearanceMenu, UiSelectionFontFamilyChoose, "Font Family...",
                  app::CommandId::SelectionFontFamilyChoose);
    appendCommand(*appearanceMenu, UiSelectionFontFamilyClear, "Inherit Document Font",
                  app::CommandId::SelectionFontFamilyClear);
    appendCommand(*appearanceMenu, UiSelectionFontSizeChoose, "Font Size...",
                  app::CommandId::SelectionFontSizeChoose);
    appendCommand(*appearanceMenu, UiSelectionFontSizeClear, "Inherit Document Size",
                  app::CommandId::SelectionFontSizeClear);
    appearanceMenu->AppendSeparator();
    appendCommand(*appearanceMenu, UiSelectionSpoilerSet, "Censor / Spoiler",
                  app::CommandId::SelectionSpoilerSet);
    appendCommand(*appearanceMenu, UiSelectionSpoilerClear, "Remove Censor / Spoiler",
                  app::CommandId::SelectionSpoilerClear);
    appearanceMenu->AppendSeparator();
    appendCommand(*appearanceMenu, UiSelectionAppearanceReset, "Reset Selection Appearance",
                  app::CommandId::SelectionAppearanceReset);
    menu.AppendSubMenu(appearanceMenu, "Selection Appearance");
    menu.AppendSeparator();

    auto* textMenu = new wxMenu();
    appendCommand(*textMenu, UiTextTrimTrailingWhitespace, "Trim Trailing Whitespace",
                  app::CommandId::TextTrimTrailingWhitespace);
    appendCommand(*textMenu, UiTextRemoveEmptyLines, "Remove Empty / Whitespace-Only Lines",
                  app::CommandId::TextRemoveEmptyLines);
    appendCommand(*textMenu, UiTextRemoveDuplicateLines, "Remove Duplicate Lines",
                  app::CommandId::TextRemoveDuplicateLines);
    textMenu->AppendSeparator();
    appendCommand(*textMenu, UiTextSortAscending, "Sort Lines Ascending",
                  app::CommandId::TextSortAscending);
    appendCommand(*textMenu, UiTextSortDescending, "Sort Lines Descending",
                  app::CommandId::TextSortDescending);
    appendCommand(*textMenu, UiTextReverseLines, "Reverse Line Order",
                  app::CommandId::TextReverseLines);
    textMenu->AppendSeparator();
    appendCommand(*textMenu, UiTextTabsToSpaces, "Tabs -> Spaces (4)",
                  app::CommandId::TextTabsToSpaces);
    appendCommand(*textMenu, UiTextSpacesToTabs, "Leading Spaces -> Tabs (4)",
                  app::CommandId::TextSpacesToTabs);
    textMenu->AppendSeparator();
    appendCommand(*textMenu, UiTextLowercaseAscii, "lowercase (ASCII)",
                  app::CommandId::TextLowercaseAscii);
    appendCommand(*textMenu, UiTextUppercaseAscii, "UPPERCASE (ASCII)",
                  app::CommandId::TextUppercaseAscii);
    menu.AppendSubMenu(textMenu, "Text Utilities");

    menu.AppendSeparator();
    appendCommand(menu, UiConvertExport, "Convert / Export...", app::CommandId::ConvertExport);
    anchor.PopupMenu(&menu, wxPoint(0, anchor.GetClientSize().y));
}

void WxMainFrame::showViewMenu(wxWindow& anchor) {
    wxMenu menu;
    appendCommand(menu, UiToggleSidebar, "Sidebar", app::CommandId::ToggleSidebar, true);
    appendCommand(menu, UiToggleTabs, "Tabs", app::CommandId::ToggleTabs, true);
    appendCommand(menu, UiToggleWrap, "Word Wrap (Current Tab)", app::CommandId::ToggleWordWrap, true);
    appendCommand(menu, UiToggleLineNumbers, "Line Numbers (Current Tab)",
                  app::CommandId::ToggleLineNumbers, true);
    appendCommand(menu, UiToggleLinks, "Clickable Links (Ctrl+Click)",
                  app::CommandId::ToggleLinkDetection, true);
    menu.AppendSeparator();

    auto* contentModeMenu = new wxMenu();
    appendCommand(*contentModeMenu, UiSwitchToEditor, "Edit Anyway (Load Full File)",
                  app::CommandId::SwitchToEditor, true);
    appendCommand(*contentModeMenu, UiSwitchToViewer, "Scalable View Mode",
                  app::CommandId::SwitchToViewer, true);
    appendCommand(*contentModeMenu, UiSwitchToHex, "Hex Preview",
                  app::CommandId::SwitchToHex, true);
    menu.AppendSubMenu(contentModeMenu, "Content Mode");
    appendCommand(menu, UiToggleFollow, "Follow File", app::CommandId::ToggleFollow, true);

    auto* performanceMenu = new wxMenu();
    appendCommand(*performanceMenu, UiViewerPerformanceAutomatic, "Automatic",
                  app::CommandId::ViewerPerformanceAutomatic, true);
    appendCommand(*performanceMenu, UiViewerPerformanceFast, "Fast",
                  app::CommandId::ViewerPerformanceFast, true);
    appendCommand(*performanceMenu, UiViewerPerformanceMemorySaver, "Memory Saver",
                  app::CommandId::ViewerPerformanceMemorySaver, true);
    menu.AppendSubMenu(performanceMenu, "View Mode Performance");

    menu.AppendSeparator();
    appendCommand(menu, UiSplitHorizontal, "Split Horizontal", app::CommandId::SplitHorizontal);
    appendCommand(menu, UiSplitVertical, "Split Vertical", app::CommandId::SplitVertical);
    appendCommand(menu, UiClosePane, "Close Active Pane", app::CommandId::ClosePane);
    menu.AppendSeparator();

    auto* themeMenu = new wxMenu();
    themeMenu->AppendRadioItem(UiThemeSystem, "System");
    themeMenu->AppendRadioItem(UiThemeLight, "Light");
    themeMenu->AppendRadioItem(UiThemeDark, "Dark");
    themeMenu->Check(UiThemeSystem, settings_.theme == settings::ThemePreference::System);
    themeMenu->Check(UiThemeLight, settings_.theme == settings::ThemePreference::Light);
    themeMenu->Check(UiThemeDark, settings_.theme == settings::ThemePreference::Dark);
    menu.AppendSubMenu(themeMenu, "Theme");

    auto* accentMenu = new wxMenu();
    accentMenu->AppendRadioItem(UiAccentViolet, "Violet");
    accentMenu->AppendRadioItem(UiAccentBlue, "Blue");
    accentMenu->AppendRadioItem(UiAccentTeal, "Teal");
    accentMenu->AppendRadioItem(UiAccentRose, "Rose");
    accentMenu->AppendRadioItem(UiAccentAmber, "Amber");
    accentMenu->Check(UiAccentViolet, settings_.accent == settings::AccentPreference::Violet);
    accentMenu->Check(UiAccentBlue, settings_.accent == settings::AccentPreference::Blue);
    accentMenu->Check(UiAccentTeal, settings_.accent == settings::AccentPreference::Teal);
    accentMenu->Check(UiAccentRose, settings_.accent == settings::AccentPreference::Rose);
    accentMenu->Check(UiAccentAmber, settings_.accent == settings::AccentPreference::Amber);
    menu.AppendSubMenu(accentMenu, "Accent");

    auto* densityMenu = new wxMenu();
    densityMenu->AppendRadioItem(UiDensityCompact, "Compact");
    densityMenu->AppendRadioItem(UiDensityComfortable, "Comfortable");
    densityMenu->Check(UiDensityCompact, settings_.density == settings::UiDensity::Compact);
    densityMenu->Check(UiDensityComfortable,
                       settings_.density == settings::UiDensity::Comfortable);
    menu.AppendSubMenu(densityMenu, "Density");

    auto* fontMenu = new wxMenu();
    wxString fontLabel{"Choose Font..."};
    if (!settings_.fontFamily.empty()) {
        fontLabel += "  [" + fromUtf8(settings_.fontFamily) + "]";
    }
    fontMenu->Append(UiChooseFont, fontLabel);
    auto* fontSizeMenu = new wxMenu();
    const auto addSize = [this, fontSizeMenu](const int id, const int size) {
        auto* item = fontSizeMenu->AppendRadioItem(id, wxString::Format("%d pt", size));
        item->Check(settings_.fontPointSize == static_cast<double>(size));
    };
    addSize(UiFontSize10, 10);
    addSize(UiFontSize11, 11);
    addSize(UiFontSize12, 12);
    addSize(UiFontSize13, 13);
    addSize(UiFontSize14, 14);
    addSize(UiFontSize16, 16);
    addSize(UiFontSize18, 18);
    addSize(UiFontSize20, 20);
    fontMenu->AppendSubMenu(fontSizeMenu, "Size");
    menu.AppendSubMenu(fontMenu, "Default Editor Font");

    menu.AppendSeparator();
    appendCommand(menu, UiSettings, "Settings...", app::CommandId::Settings);

    anchor.PopupMenu(&menu, wxPoint(0, anchor.GetClientSize().y));
}

void WxMainFrame::appendCommand(wxMenu& menu,
                                const int uiId,
                                const wxString& label,
                                const app::CommandId command,
                                const bool checkable) {
    wxMenuItem* item = checkable ? menu.AppendCheckItem(uiId, label) : menu.Append(uiId, label);
    const auto state = app::CommandCatalog::state(command, presentation_.commandContext());
    item->Enable(state.enabled && dispatcher_.hasHandler(command));
    if (checkable) {
        item->Check(state.checked);
    }
}

void WxMainFrame::appendRadioCommand(wxMenu& menu,
                                     const int uiId,
                                     const wxString& label,
                                     const app::CommandId command) {
    auto* item = menu.AppendRadioItem(uiId, label);
    const auto state = app::CommandCatalog::state(command, presentation_.commandContext());
    item->Enable(state.enabled && dispatcher_.hasHandler(command));
    item->Check(state.checked);
}

void WxMainFrame::dispatch(const app::CommandId command) {
    const auto result = dispatcher_.dispatch(command, presentation_.commandContext());
    if (result.error && result.handled) {
        showError("Operation failed", result.error);
    }
    scheduleSynchronize();
}

std::error_code WxMainFrame::newDocument() {
    const auto pane = workspace_.activePane() ? workspace_.activePane() : workspace_.primaryPane();
    return newDocumentInPane(pane);
}

std::error_code WxMainFrame::newDocumentInPane(const workspace::PaneId pane) {
    if (workspace_.pane(pane) == nullptr) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    const auto document = documents_.createUntitled();
    if (!document) {
        return std::make_error_code(std::errc::not_enough_memory);
    }
    const auto view = shell_.openDocumentInPane(document, pane);
    if (!view) {
        static_cast<void>(documents_.close(document));
        return std::make_error_code(std::errc::invalid_argument);
    }
    static_cast<void>(lifecycle_.saveSession());
    shell_.requestRefresh();
    return {};
}

std::error_code WxMainFrame::openFilesDialog() {
    wxFileDialog dialog(this, "Open File", wxEmptyString, wxEmptyString,
                        "All files (*.*)|*.*",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
    if (dialog.ShowModal() != wxID_OK) {
        return {};
    }

    wxArrayString paths;
    dialog.GetPaths(paths);
    for (const auto& value : paths) {
        static_cast<void>(openPath(toPath(value)));
    }
    return {};
}

std::error_code WxMainFrame::chooseSidebarFolder() {
    const wxString initial = sidebarRoot_.empty() ? wxString{} : fromUtf8(pathUtf8(sidebarRoot_));
    wxDirDialog dialog(this, "Choose a folder to browse and search", initial,
                       wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) {
        return {};
    }

    const auto root = toPath(dialog.GetPath());
    if (root.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    stopSidebarSearch();
    stopSidebarIndex();
    sidebarRoot_ = root.lexically_normal();
    sidebarScope_ = sidebarRoot_;
    sidebarRootUnavailable_ = false;
    sidebarIndexedFiles_ = 0U;
    sidebarIndexTruncated_ = false;
    sidebarIndexAvailable_ = false;
    fileSearch_.clear();
    presentation_.setSidebarFolderActive(true);
    resetSidebarTree();
    updateSidebarHint();
    shell_.requestRefresh();
    scheduleSynchronize();
    settings_.sidebarRootUtf8 = pathUtf8(sidebarRoot_);
    persistSettings();
    return {};
}

std::error_code WxMainFrame::reindexSidebarFolder() {
    if (sidebarRoot_.empty()) {
        return std::make_error_code(std::errc::operation_not_permitted);
    }
    return startSidebarIndex(sidebarRoot_);
}

std::error_code WxMainFrame::clearSidebarFolder() {
    stopSidebarSearch();
    stopSidebarIndex();
    sidebarRoot_.clear();
    sidebarScope_.clear();
    sidebarRootUnavailable_ = false;
    sidebarIndexedFiles_ = 0U;
    sidebarIndexTruncated_ = false;
    sidebarIndexAvailable_ = false;
    fileSearch_.clear();
    presentation_.setSidebarFolderActive(false);
    resetSidebarTree();
    updateSidebarHint();
    shell_.requestRefresh();
    scheduleSynchronize();
    settings_.sidebarRootUtf8.clear();
    persistSettings();
    return {};
}

std::error_code WxMainFrame::saveActive() {
    const auto documentId = activeDocumentId();
    auto* document = activeDocument();
    if (!documentId || document == nullptr) {
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }
    if (document->path().empty()) {
        return saveActiveAs(false);
    }

    auto error = document->save();
    if (error == std::errc::text_file_busy) {
        const auto answer = showThemedConfirmation(
            this, theme_, "someone touched your file while u werent looking",
            "The file changed on disk after it was opened. Overwrite the external version?",
            "Overwrite", true);
        if (answer != wxID_YES) {
            return {};
        }
        core::SaveOptions overwrite;
        overwrite.allowExternalOverwrite = true;
        error = document->save(overwrite);
    }

    if (!error) {
        static_cast<void>(lifecycle_.forget(*documentId));
        presentation_.setRecovered(*documentId, false);
        presentation_.setExternalChangeState(*documentId, storage::FileChangeState::Unchanged);
        static_cast<void>(lifecycle_.noteOpened(
            document->path(), document->profile().recommendedMode));
        static_cast<void>(lifecycle_.saveSession());
        static_cast<void>(persistTextAppearance(*documentId));
        shell_.requestRefresh();
    }
    return error;
}

std::error_code WxMainFrame::saveActiveOverwriteExternal() {
    const auto documentId = activeDocumentId();
    auto* document = activeDocument();
    if (!documentId || document == nullptr) {
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }
    if (document->path().empty()) {
        return saveActiveAs(false);
    }

    core::SaveOptions overwrite;
    overwrite.allowExternalOverwrite = true;
    const auto error = document->save(overwrite);
    if (!error) {
        static_cast<void>(lifecycle_.forget(*documentId));
        presentation_.setRecovered(*documentId, false);
        presentation_.setExternalChangeState(*documentId, storage::FileChangeState::Unchanged);
        static_cast<void>(lifecycle_.noteOpened(
            document->path(), document->profile().recommendedMode));
        static_cast<void>(lifecycle_.saveSession());
        static_cast<void>(persistTextAppearance(*documentId));
        shell_.requestRefresh();
    }
    return error;
}

std::error_code WxMainFrame::reloadActive() {
    const auto documentId = activeDocumentId();
    auto* document = activeDocument();
    if (!documentId || document == nullptr || document->path().empty()) {
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }

    if (document->modified()) {
        const auto answer = showThemedConfirmation(
            this, theme_, "Reload from disk",
            "Reloading will discard the unsaved changes in this document.",
            "Reload", true);
        if (answer != wxID_YES) {
            return {};
        }
    }

    const auto path = document->path();
    const auto error = documents_.reloadRouted(*documentId, settings_.inspectOptions);
    if (error) {
        return error;
    }
    document = documents_.get(*documentId);
    if (document == nullptr) {
        return std::make_error_code(std::errc::state_not_recoverable);
    }
    workspace_.forEachView([this, documentId, document](const workspace::ViewState& view) {
        if (view.document != *documentId) return;
        auto state = presentation_.viewRuntime(view.id) != nullptr
                         ? *presentation_.viewRuntime(view.id)
                         : app::ViewRuntimeState{};
        state.openMode = document->profile().recommendedMode;
        state.viewerPerformance = settings_.viewerPerformance;
        state.followEnabled = false;
        static_cast<void>(presentation_.setViewRuntime(view.id, state));
    });
    static_cast<void>(lifecycle_.forget(*documentId));
    presentation_.setRecovered(*documentId, false);
    presentation_.setExternalChangeState(*documentId, storage::FileChangeState::Unchanged);
    resetTextAppearance(*documentId);
    if (runtime_ != nullptr) {
        runtime_->invalidateDocument(*documentId);
    }
    static_cast<void>(lifecycle_.saveSession());
    shell_.requestRefresh();
    return {};
}

std::error_code WxMainFrame::saveActiveAs(const bool copy) {
    const auto documentId = activeDocumentId();
    auto* document = activeDocument();
    if (!documentId || document == nullptr) {
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }

    wxString defaultName;
    if (!document->path().empty()) {
        defaultName = fromUtf8(pathUtf8(document->path().filename()));
    }
    wxFileDialog dialog(this,
                        copy ? "Save Copy" : "Save As",
                        wxEmptyString,
                        defaultName,
                        "All files (*.*)|*.*",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) {
        return {};
    }

    const auto path = toPath(dialog.GetPath());
    const auto error = copy ? document->saveCopy(path) : document->saveAs(path);
    if (!error) {
        if (!copy) {
            static_cast<void>(lifecycle_.forget(*documentId));
            presentation_.setRecovered(*documentId, false);
            presentation_.setExternalChangeState(*documentId, storage::FileChangeState::Unchanged);
            static_cast<void>(lifecycle_.noteOpened(
                path, document->profile().recommendedMode));
            static_cast<void>(lifecycle_.saveSession());
        }
        if (const auto colors = appearanceStates_.find(*documentId);
            colors != appearanceStates_.end() && colors->second.loaded) {
            static_cast<void>(
                colors->second.appearance.empty()
                    ? metadataStore_.erase(path)
                    : metadataStore_.save(path, document->text(),
                                          colors->second.appearance));
        }
        shell_.requestRefresh();
    }
    return error;
}

std::error_code WxMainFrame::closeActiveView() {
    const auto viewId = activeView();
    return viewId ? closeView(*viewId) : std::error_code{};
}

std::error_code WxMainFrame::closeView(const workspace::ViewId viewId) {
    const auto* viewState = workspace_.view(viewId);
    if (viewState == nullptr) {
        return {};
    }
    const auto documentId = viewState->document;
    auto* document = documents_.get(documentId);
    if (document != nullptr && document->modified() &&
        !documentViewedOutsideView(documentId, viewId)) {
        const auto answer = showThemedConfirmation(
            this, theme_, "Unsaved changes",
            "This tab has unsaved changes and is the last open view of this document.",
            "Close tab", true);
        if (answer != wxID_YES) {
            return {};
        }
    }

    if (!shell_.closeView(viewId)) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    if (!documentStillViewed(documentId)) {
        static_cast<void>(lifecycle_.forget(documentId));
        presentation_.setRecovered(documentId, false);
        appearanceStates_.erase(documentId);
        static_cast<void>(documents_.close(documentId));
    }
    if (workspace_.viewCount() == 0U) {
        createInitialDocument();
    }
    static_cast<void>(lifecycle_.saveSession());
    shell_.requestRefresh();
    return {};
}

std::error_code WxMainFrame::closeActivePane() {
    const auto paneId = workspace_.activePane();
    const auto* pane = workspace_.pane(paneId);
    if (pane == nullptr || workspace_.paneCount() <= 1U) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    std::vector<core::DocumentId> documentsClosing;
    for (const auto viewId : pane->views) {
        const auto* view = workspace_.view(viewId);
        if (view == nullptr || documentViewedOutsidePane(view->document, paneId)) {
            continue;
        }
        if (std::find(documentsClosing.begin(), documentsClosing.end(), view->document) ==
            documentsClosing.end()) {
            documentsClosing.push_back(view->document);
        }
    }

    std::size_t modifiedOnlyHere = 0U;
    for (const auto documentId : documentsClosing) {
        const auto* document = documents_.get(documentId);
        if (document != nullptr && document->modified()) {
            ++modifiedOnlyHere;
        }
    }
    if (modifiedOnlyHere != 0U) {
        wxString message;
        if (modifiedOnlyHere == 1U) {
            message =
                "Closing this pane will discard unsaved changes in one document that is not "
                "open in another pane. Continue?";
        } else {
            message = wxString::Format(
                "Closing this pane will discard unsaved changes in %llu documents that are not "
                "open in another pane. Continue?",
                static_cast<unsigned long long>(modifiedOnlyHere));
        }
        if (showThemedConfirmation(this, theme_, "Close Pane", message,
                                   "Close pane", true) != wxID_YES) {
            return {};
        }
    }

    static_cast<void>(shell_.closePane(paneId));
    if (workspace_.pane(paneId) != nullptr) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    for (const auto documentId : documentsClosing) {
        if (documentStillViewed(documentId)) {
            continue;
        }
        static_cast<void>(lifecycle_.forget(documentId));
        presentation_.setRecovered(documentId, false);
        appearanceStates_.erase(documentId);
        static_cast<void>(documents_.close(documentId));
    }
    if (workspace_.viewCount() == 0U) {
        createInitialDocument();
    }
    static_cast<void>(lifecycle_.saveSession());
    shell_.requestRefresh();
    scheduleSynchronize();
    return {};
}

std::error_code WxMainFrame::splitActive(const workspace::SplitOrientation orientation) {
    const auto pane = workspace_.activePane();
    if (!pane) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    const auto result = shell_.splitPane(pane, orientation);
    if (!result) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    if (const auto viewId = activeView()) {
        static_cast<void>(shell_.duplicateView(*viewId, result.pane));
    }
    static_cast<void>(lifecycle_.saveSession());
    shell_.requestRefresh();
    return {};
}

std::error_code WxMainFrame::toggleSidebar() {
    const auto visible = presentation_.commandContext().sidebarVisible;
    settings_.sidebarVisible = !visible;
    shell_.setSidebarVisible(settings_.sidebarVisible);

    if (!settings_.sidebarVisible) {
        stopSidebarSearch();
        stopSidebarIndex();
        fileSearch_.clear();
        sidebarIndexedFiles_ = 0U;
        sidebarIndexAvailable_ = false;
    } else if (!sidebarRoot_.empty()) {
        std::error_code rootError;
        sidebarRootUnavailable_ = !std::filesystem::is_directory(sidebarRoot_, rootError) ||
                                  static_cast<bool>(rootError);
        resetSidebarTree();
        updateSidebarHint();
    }

    persistSettings();
    shell_.requestRefresh();
    scheduleSynchronize();
    return {};
}

std::error_code WxMainFrame::toggleTabs() {
    settings_.tabsEnabled = !settings_.tabsEnabled;
    tabsFingerprint_.reset();
    shell_.requestRefresh();
    persistSettings();
    return {};
}

std::error_code WxMainFrame::toggleWordWrap() {
    const auto viewId = activeView();
    if (!viewId) {
        return {};
    }
    auto* viewState = workspace_.view(*viewId);
    if (viewState == nullptr) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    auto state = presentation_.viewRuntime(*viewId) != nullptr
                     ? *presentation_.viewRuntime(*viewId)
                     : app::ViewRuntimeState{};
    state.wordWrap = !state.wordWrap;
    viewState->wordWrapOverride = state.wordWrap;
    static_cast<void>(presentation_.setViewRuntime(*viewId, state));
    shell_.requestRefresh();
    static_cast<void>(lifecycle_.saveSession());
    return {};
}

std::error_code WxMainFrame::toggleLineNumbers() {
    const auto viewId = activeView();
    if (!viewId) {
        return {};
    }
    auto* viewState = workspace_.view(*viewId);
    if (viewState == nullptr) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    auto state = presentation_.viewRuntime(*viewId) != nullptr
                     ? *presentation_.viewRuntime(*viewId)
                     : app::ViewRuntimeState{};
    state.lineNumbersVisible = !state.lineNumbersVisible;
    viewState->lineNumbersOverride = state.lineNumbersVisible;
    static_cast<void>(presentation_.setViewRuntime(*viewId, state));
    shell_.requestRefresh();
    static_cast<void>(lifecycle_.saveSession());
    return {};
}

std::error_code WxMainFrame::toggleLinkDetection() {
    settings_.highlightUrls = !settings_.highlightUrls;
    persistSettings();
    refreshEditorAppearance();
    return {};
}

std::error_code WxMainFrame::reopenActiveAsEncoding(
    const encoding::Encoding encoding) {
    const auto documentId = activeDocumentId();
    auto* document = activeDocument();
    if (!documentId || document == nullptr || document->path().empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    if (document->modified()) {
        return std::make_error_code(std::errc::text_file_busy);
    }
    const auto currentMode = presentation_.commandContext().openMode;
    if (document->textBufferLoaded() ||
        (currentMode != core::OpenMode::BinaryPreview &&
         currentMode != core::OpenMode::Viewer)) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    const auto path = document->path();
    const auto maximumBytes = currentMode == core::OpenMode::Viewer
                                  ? std::size_t{0U}
                                  : core::Document::defaultEditorLoadLimit;
    const auto error = documents_.reloadAsEncoding(*documentId, encoding, maximumBytes);
    if (error) {
        return error;
    }
    const auto nextMode = document->profile().recommendedMode;

    workspace_.forEachView([this, documentId](const workspace::ViewState& view) {
        if (view.document == *documentId) static_cast<void>(presentation_.eraseViewRuntime(view.id));
    });
    resetTextAppearance(*documentId);
    if (runtime_ != nullptr) {
        runtime_->invalidateDocument(*documentId);
    }
    static_cast<void>(lifecycle_.noteOpened(path, nextMode));
    static_cast<void>(lifecycle_.saveSession());
    shell_.requestRefresh();
    scheduleSynchronize();
    return {};
}

std::error_code WxMainFrame::switchActiveToEditor() {
    const auto documentId = activeDocumentId();
    const auto viewId = activeView();
    auto* document = activeDocument();
    if (!documentId || !viewId || document == nullptr || document->path().empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    if (document->profile().encoding.binaryLike) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    if (!document->textBufferLoaded()) {
        const auto fileBytes = static_cast<std::uint64_t>(document->profile().fileSize);
        // u clicked edit anyway, the ram situation is now between you and god
        const auto* title = fileBytes < 256ULL * 1024ULL * 1024ULL
                                ? "you really wanna do this huh"
                            : fileBytes < 1024ULL * 1024ULL * 1024ULL
                                ? "this is a text editor not a hostage situation"
                                : "did you just tried to open your femboy furry feet collection?";
        wxString message = "Edit Anyway will load the entire file (";
        message += formatByteSize(fileBytes);
        message += ") into memory. This can use substantially more RAM than View Mode "
                   "and may exhaust available memory or make the app unresponsive. "
                   "The file on disk will not change until you save. Continue?";
        if (wxMessageBox(message, title,
                         wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
            return {};
        }
        if (document->profile().fileSize >
            static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
            return std::make_error_code(std::errc::file_too_large);
        }
        const auto error = documents_.materializeForEdit(
            *documentId, static_cast<std::size_t>(document->profile().fileSize));
        if (error) {
            return error;
        }
    }

    auto state = presentation_.viewRuntime(*viewId) != nullptr
                     ? *presentation_.viewRuntime(*viewId)
                     : app::ViewRuntimeState{};
    state.openMode = core::OpenMode::Editor;
    state.followEnabled = false;
    state.contentBytes = 0U;
    state.windowByteStart = 0U;
    state.windowByteEnd = 0U;
    state.viewerCacheResidentBytes = 0U;
    static_cast<void>(presentation_.setViewRuntime(*viewId, state));
    if (runtime_ != nullptr) {
        runtime_->invalidateDocument(*documentId);
    }
    static_cast<void>(lifecycle_.noteOpened(document->path(), core::OpenMode::Editor));
    static_cast<void>(lifecycle_.saveSession());
    shell_.requestRefresh();
    return {};
}

std::error_code WxMainFrame::switchActiveToHexPreview() {
    const auto documentId = activeDocumentId();
    const auto viewId = activeView();
    auto* document = activeDocument();
    if (!documentId || !viewId || document == nullptr || document->path().empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    if (document->modified()) {
        return std::make_error_code(std::errc::text_file_busy);
    }

    auto state = presentation_.viewRuntime(*viewId) != nullptr
                     ? *presentation_.viewRuntime(*viewId)
                     : app::ViewRuntimeState{};
    state.openMode = core::OpenMode::BinaryPreview;
    state.followEnabled = false;
    state.contentBytes = 0U;
    state.windowByteStart = 0U;
    state.windowByteEnd = 0U;
    state.viewerCacheResidentBytes = 0U;
    static_cast<void>(presentation_.setViewRuntime(*viewId, state));
    if (runtime_ != nullptr) {
        runtime_->invalidateDocument(*documentId);
    }
    static_cast<void>(lifecycle_.noteOpened(document->path(), core::OpenMode::BinaryPreview));
    static_cast<void>(lifecycle_.saveSession());
    shell_.requestRefresh();
    return {};
}

std::error_code WxMainFrame::switchActiveToViewer() {
    const auto documentId = activeDocumentId();
    auto* document = activeDocument();
    if (!documentId || document == nullptr || document->path().empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    if (document->profile().encoding.binaryLike) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    if (document->modified()) {
        if (wxMessageBox(
                "Switching to scalable View Mode releases the editable in-memory buffer and "
                "will discard unsaved changes. Continue?",
                "Switch to View Mode",
                wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
                this) != wxYES) {
            return {};
        }
    }

    const auto path = document->path();
    const auto error = documents_.dematerializeForViewer(
        *documentId, settings_.inspectOptions, document->modified());
    if (error) {
        return error;
    }

    workspace_.forEachView([this, documentId](const workspace::ViewState& view) {
        if (view.document != *documentId) return;
        auto state = presentation_.viewRuntime(view.id) != nullptr
                         ? *presentation_.viewRuntime(view.id)
                         : app::ViewRuntimeState{};
        state.openMode = core::OpenMode::Viewer;
        state.viewerPerformance = settings_.viewerPerformance;
        state.resolvedViewerPerformance = settings_.viewerPerformance;
        state.followEnabled = false;
        state.contentBytes = 0U;
        state.windowByteStart = 0U;
        state.windowByteEnd = 0U;
        state.viewerCacheResidentBytes = 0U;
        static_cast<void>(presentation_.setViewRuntime(view.id, state));
    });

    static_cast<void>(lifecycle_.forget(*documentId));
    if (runtime_ != nullptr) {
        runtime_->invalidateDocument(*documentId);
    }
    static_cast<void>(lifecycle_.noteOpened(path, core::OpenMode::Viewer));
    static_cast<void>(lifecycle_.saveSession());
    shell_.requestRefresh();
    return {};
}

std::error_code WxMainFrame::toggleFollow() {
    const auto viewId = activeView();
    if (!viewId) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    const auto* current = presentation_.viewRuntime(*viewId);
    if (current == nullptr || current->openMode != core::OpenMode::Viewer) {
        return std::make_error_code(std::errc::operation_not_permitted);
    }

    auto next = *current;
    next.followEnabled = !current->followEnabled;
    static_cast<void>(presentation_.setViewRuntime(*viewId, next));
    if (next.followEnabled) {
        if (const auto document = activeDocumentId()) {
            presentation_.setExternalConflict(*document, false);
        }
    }
    shell_.requestRefresh();
    scheduleSynchronize();
    return {};
}

std::error_code WxMainFrame::setViewerPerformance(const viewer::PerformanceProfile profile) {
    const auto viewId = activeView();
    if (!viewId) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    const auto* current = presentation_.viewRuntime(*viewId);
    if (current == nullptr || current->openMode != core::OpenMode::Viewer) {
        return std::make_error_code(std::errc::operation_not_permitted);
    }

    auto next = *current;
    next.viewerPerformance = profile;
    next.resolvedViewerPerformance = profile;
    next.viewerCacheResidentBytes = 0U;
    static_cast<void>(presentation_.setViewRuntime(*viewId, next));
    settings_.viewerPerformance = profile;
    persistSettings();
    shell_.requestRefresh();
    return {};
}

std::error_code WxMainFrame::setDocumentEncoding(const encoding::Encoding encoding) {
    const auto documentId = activeDocumentId();
    auto* document = activeDocument();
    if (!documentId || document == nullptr || !document->textBufferLoaded()) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    const auto error = document->setSaveEncoding(encoding);
    if (error) {
        return error;
    }
    if (document->modified()) {
        lifecycle_.noteEdited(*documentId);
    }
    shell_.requestRefresh();
    return {};
}

std::error_code WxMainFrame::toggleDocumentBom() {
    const auto documentId = activeDocumentId();
    auto* document = activeDocument();
    if (!documentId || document == nullptr || !document->textBufferLoaded()) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    const auto error = document->setWritesBom(!document->writesBom());
    if (error) {
        return error;
    }
    if (document->modified()) {
        lifecycle_.noteEdited(*documentId);
    }
    shell_.requestRefresh();
    return {};
}

std::error_code WxMainFrame::convertLineEndings(const core::LineEndingPolicy policy) {
    const auto viewId = activeView();
    auto* document = activeDocument();
    if (!viewId || document == nullptr || !document->textBufferLoaded() || runtime_ == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    const auto converted = core::TextTransform::normalizeLineEndings(document->text(), policy);
    if (converted == document->text()) {
        return {};
    }
    if (!runtime_->replaceAllText(*viewId, converted)) {
        return std::make_error_code(std::errc::io_error);
    }
    searchSession_.match.reset();
    shell_.requestRefresh();
    scheduleSynchronize();
    return {};
}

std::error_code WxMainFrame::validateActiveJson() {
    const auto* document = activeDocument();
    if (document == nullptr || !document->textBufferLoaded()) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    const auto validation = formats::FormatTransform::validateJson(document->text());
    if (validation.valid) {
        wxMessageBox("Valid JSON.", "JSON Validation", wxOK | wxICON_INFORMATION, this);
        return {};
    }

    wxString message = wxString::Format(
        "Invalid JSON at line %llu, column %llu.",
        static_cast<unsigned long long>(validation.line),
        static_cast<unsigned long long>(validation.column));
    if (!validation.message.empty()) {
        message += "\n\n" + fromUtf8(validation.message);
    }
    wxMessageBox(message, "JSON Validation", wxOK | wxICON_WARNING, this);
    return {};
}

std::error_code WxMainFrame::formatActiveJson(const bool pretty) {
    const auto viewId = activeView();
    auto* document = activeDocument();
    if (!viewId || document == nullptr || !document->textBufferLoaded() || runtime_ == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    const auto transformed = pretty
                                 ? formats::FormatTransform::prettyJson(document->text())
                                 : formats::FormatTransform::minifyJson(document->text());
    if (!transformed) {
        wxString message = wxString::Format(
            "JSON transform failed at line %llu, column %llu.",
            static_cast<unsigned long long>(transformed.validation.line),
            static_cast<unsigned long long>(transformed.validation.column));
        if (!transformed.validation.message.empty()) {
            message += "\n\n" + fromUtf8(transformed.validation.message);
        }
        wxMessageBox(message, pretty ? "Pretty JSON" : "Minify JSON",
                     wxOK | wxICON_WARNING, this);
        return {};
    }

    auto output = transformed.text;
    if (pretty) {
        const auto linePolicy = lineEndingPolicyFor(document->profile().lineEnding);
        output = core::TextTransform::normalizeLineEndings(output, linePolicy);
    }
    if (output == document->text()) {
        return {};
    }
    if (!runtime_->replaceAllText(*viewId, output)) {
        return std::make_error_code(std::errc::io_error);
    }
    searchSession_.match.reset();
    shell_.requestRefresh();
    scheduleSynchronize();
    return {};
}

std::error_code WxMainFrame::showConvertExportDialog() {
    const auto* document = activeDocument();
    if (document == nullptr || !document->textBufferLoaded()) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    struct TransformChoice final {
        wxString label;
        core::ContentTransform transform{core::ContentTransform::None};
    };
    std::vector<TransformChoice> transforms;
    transforms.push_back({"No content transform", core::ContentTransform::None});
    switch (document->profile().format.format) {
    case formats::TextFormat::Json:
        transforms.push_back({"JSON - Pretty", core::ContentTransform::JsonPretty});
        transforms.push_back({"JSON - Minify", core::ContentTransform::JsonMinify});
        break;
    case formats::TextFormat::Csv:
        transforms.push_back({"CSV -> TSV", core::ContentTransform::CsvToTsv});
        break;
    case formats::TextFormat::Tsv:
        transforms.push_back({"TSV -> CSV", core::ContentTransform::TsvToCsv});
        break;
    default:
        break;
    }

    constexpr std::array encodings{
        encoding::Encoding::Utf8,
        encoding::Encoding::Utf16LE,
        encoding::Encoding::Utf16BE,
        encoding::Encoding::Utf32LE,
        encoding::Encoding::Utf32BE,
        encoding::Encoding::Windows1252,
        encoding::Encoding::Windows1254,
    };
    const std::array<wxString, encodings.size()> encodingLabels{
        "UTF-8", "UTF-16 LE", "UTF-16 BE", "UTF-32 LE", "UTF-32 BE",
        "Windows-1252", "Windows-1254 (Turkish)"};
    constexpr std::array linePolicies{
        core::LineEndingPolicy::Preserve,
        core::LineEndingPolicy::LF,
        core::LineEndingPolicy::CRLF,
        core::LineEndingPolicy::CR,
    };
    const std::array<wxString, linePolicies.size()> lineLabels{
        "Preserve current", "LF", "CRLF", "CR"};

    wxDialog dialog(this, wxID_ANY, "Convert / Export", wxDefaultPosition, wxDefaultSize,
                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto addChoice = [&](const wxString& label, const wxArrayString& values) {
        root->Add(new wxStaticText(&dialog, wxID_ANY, label), 0, wxLEFT | wxRIGHT | wxTOP, 12);
        auto* choice = new wxChoice(&dialog, wxID_ANY, wxDefaultPosition, wxDefaultSize, values);
        root->Add(choice, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
        return choice;
    };

    wxArrayString transformLabels;
    for (const auto& item : transforms) {
        transformLabels.Add(item.label);
    }
    auto* transformChoice = addChoice("Content transform", transformLabels);
    transformChoice->SetSelection(0);

    wxArrayString exportEncodingLabels;
    for (const auto& label : encodingLabels) {
        exportEncodingLabels.Add(label);
    }
    auto* encodingChoice = addChoice("Encoding", exportEncodingLabels);
    int encodingSelection = 0;
    for (std::size_t index = 0U; index < encodings.size(); ++index) {
        if (encodings[index] == document->saveEncoding()) {
            encodingSelection = static_cast<int>(index);
            break;
        }
    }
    encodingChoice->SetSelection(encodingSelection);

    wxArrayString exportLineLabels;
    for (const auto& label : lineLabels) {
        exportLineLabels.Add(label);
    }
    auto* lineChoice = addChoice("Line endings", exportLineLabels);
    lineChoice->SetSelection(0);

    auto* bom = new wxCheckBox(&dialog, wxID_ANY, "Write Byte Order Mark (BOM)");
    bom->SetValue(document->writesBom());
    root->Add(bom, 0, wxLEFT | wxRIGHT | wxTOP, 12);

    wxArrayString indentLabels;
    indentLabels.Add("2 spaces");
    indentLabels.Add("4 spaces");
    auto* indentChoice = addChoice("JSON pretty indent", indentLabels);
    indentChoice->SetSelection(0);
    indentChoice->Enable(false);

    const auto refreshOptions = [=](wxCommandEvent* event) {
        const auto encodingIndex = encodingChoice->GetSelection();
        const bool bomSupported = encodingIndex != wxNOT_FOUND &&
                                  encodingSupportsBom(encodings[static_cast<std::size_t>(encodingIndex)]);
        bom->Enable(bomSupported);
        if (!bomSupported) {
            bom->SetValue(false);
        }
        const auto transformIndex = transformChoice->GetSelection();
        const bool prettySelected = transformIndex != wxNOT_FOUND &&
                                    transforms[static_cast<std::size_t>(transformIndex)].transform ==
                                        core::ContentTransform::JsonPretty;
        indentChoice->Enable(prettySelected);
        if (event != nullptr) {
            event->Skip();
        }
    };
    encodingChoice->Bind(wxEVT_CHOICE, [refreshOptions](wxCommandEvent& event) mutable {
        refreshOptions(&event);
    });
    transformChoice->Bind(wxEVT_CHOICE, [refreshOptions](wxCommandEvent& event) mutable {
        refreshOptions(&event);
    });
    refreshOptions(nullptr);

    if (auto* buttons = dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL); buttons != nullptr) {
        root->Add(buttons, 0, wxEXPAND | wxALL, 12);
    }
    dialog.SetSizerAndFit(root);
    dialog.SetMinSize(wxSize(420, dialog.GetSize().GetHeight()));
    dialog.CentreOnParent();
    if (dialog.ShowModal() != wxID_OK) {
        return {};
    }

    const auto transformIndex = transformChoice->GetSelection();
    const auto selectedTransform = transformIndex == wxNOT_FOUND
                                       ? core::ContentTransform::None
                                       : transforms[static_cast<std::size_t>(transformIndex)].transform;

    std::filesystem::path suggestedName = document->path().empty()
                                              ? std::filesystem::path{"export.txt"}
                                              : document->path().filename();
    if (selectedTransform == core::ContentTransform::CsvToTsv) {
        suggestedName.replace_extension(".tsv");
    } else if (selectedTransform == core::ContentTransform::TsvToCsv) {
        suggestedName.replace_extension(".csv");
    }

    wxFileDialog fileDialog(this, "Export File", wxEmptyString,
                            fromUtf8(pathUtf8(suggestedName)), "All files (*.*)|*.*",
                            wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (fileDialog.ShowModal() != wxID_OK) {
        return {};
    }
    const auto outputPath = toPath(fileDialog.GetPath());

    if (!document->path().empty()) {
        std::error_code equivalentError;
        if (std::filesystem::equivalent(document->path(), outputPath, equivalentError) &&
            !equivalentError) {
            wxMessageBox(
                "Convert / Export cannot overwrite the currently open source file. Use Save or "
                "Save As for in-place document changes.",
                "Convert / Export", wxOK | wxICON_WARNING, this);
            return {};
        }
    }

    const auto selectedEncodingIndex = encodingChoice->GetSelection();
    const auto selectedLineIndex = lineChoice->GetSelection();
    if (selectedEncodingIndex == wxNOT_FOUND || selectedLineIndex == wxNOT_FOUND) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    core::ExportOptions options;
    options.transform = selectedTransform;
    options.jsonIndentWidth = indentChoice->GetSelection() == 1 ? 4U : 2U;
    options.save.encoding = encodings[static_cast<std::size_t>(selectedEncodingIndex)];
    options.save.writeBom = bom->GetValue();
    options.save.lineEnding = linePolicies[static_cast<std::size_t>(selectedLineIndex)];
    if (options.save.lineEnding == core::LineEndingPolicy::Preserve &&
        selectedTransform != core::ContentTransform::None) {
        options.save.lineEnding = lineEndingPolicyFor(document->profile().lineEnding);
    }

    const auto exported = core::ExportService::write(outputPath, document->text(), options);
    if (!exported) {
        if (!exported.validation.valid && !exported.validation.message.empty()) {
            wxString message = wxString::Format(
                "Conversion failed at line %llu, column %llu.",
                static_cast<unsigned long long>(exported.validation.line),
                static_cast<unsigned long long>(exported.validation.column));
            message += "\n\n" + fromUtf8(exported.validation.message);
            wxMessageBox(message, "Convert / Export", wxOK | wxICON_WARNING, this);
            return {};
        }
        return exported.error;
    }

    shell_.requestRefresh();
    return {};
}

std::error_code WxMainFrame::applyTextUtility(const app::CommandId command) {
    const auto documentId = activeDocumentId();
    const auto viewId = activeView();
    auto* document = activeDocument();
    if (!documentId || !viewId || document == nullptr || !document->textBufferLoaded() ||
        runtime_ == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    static_cast<void>(runtime_->synchronize());
    auto* host = runtime_->host(*viewId);
    if (host == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    const auto hostRuntime = host->runtime();
    const auto& documentText = document->text();
    const auto documentSize = documentText.size();
    const auto rawStart = std::min(hostRuntime.caretOffset, hostRuntime.anchorOffset);
    const auto rawEnd = std::max(hostRuntime.caretOffset, hostRuntime.anchorOffset);
    const auto start = std::min(rawStart, documentSize);
    const auto end = std::min(rawEnd, documentSize);
    const bool selectionOnly = start < end;
    const std::string_view input = selectionOnly
                                       ? std::string_view{documentText}.substr(start, end - start)
                                       : std::string_view{documentText};

    std::string output;
    switch (command) {
    case app::CommandId::TextTrimTrailingWhitespace:
        output = core::TextUtilities::trimTrailingWhitespace(input);
        break;
    case app::CommandId::TextRemoveEmptyLines:
        output = core::TextUtilities::removeEmptyLines(input, true);
        break;
    case app::CommandId::TextRemoveDuplicateLines:
        output = core::TextUtilities::removeDuplicateLines(input);
        break;
    case app::CommandId::TextSortAscending:
        output = core::TextUtilities::sortLines(input, core::LineSortDirection::Ascending);
        break;
    case app::CommandId::TextSortDescending:
        output = core::TextUtilities::sortLines(input, core::LineSortDirection::Descending);
        break;
    case app::CommandId::TextReverseLines:
        output = core::TextUtilities::reverseLines(input);
        break;
    case app::CommandId::TextTabsToSpaces:
        output = core::TextUtilities::expandTabs(input, 4U);
        break;
    case app::CommandId::TextSpacesToTabs:
        output = core::TextUtilities::compressLeadingSpacesToTabs(input, 4U);
        break;
    case app::CommandId::TextLowercaseAscii:
        output = core::TextUtilities::asciiToLower(input);
        break;
    case app::CommandId::TextUppercaseAscii:
        output = core::TextUtilities::asciiToUpper(input);
        break;
    default:
        return std::make_error_code(std::errc::invalid_argument);
    }

    if (output == input) {
        return {};
    }

    const bool preservesOffsets =
        output.size() == input.size() &&
        (command == app::CommandId::TextLowercaseAscii ||
         command == app::CommandId::TextUppercaseAscii);
    const auto colorPolicy = preservesOffsets
                                 ? metadata::TextColorEditPolicy::PreserveOffsets
                                 : metadata::TextColorEditPolicy::AdjustRanges;

    if (!preservesOffsets) {
        ensureTextAppearanceState(*documentId);
        const auto colors = appearanceStates_.find(*documentId);
        if (colors != appearanceStates_.end() && colors->second.appearance.intersects(start, end)) {
            const auto answer = showThemedConfirmation(
                this, theme_, "Selection appearance metadata",
                "This utility changes text positions. Selection appearance in the affected range "
                "cannot be mapped safely and may be adjusted or removed. Continue?",
                "Transform", true);
            if (answer != wxID_YES) {
                return {};
            }
        }
    }

    bool replaced = false;
    if (selectionOnly) {
        replaced = runtime_->replaceTextRange(
            *viewId,
            search::SearchMatch{static_cast<std::uint64_t>(start),
                                static_cast<std::uint64_t>(end - start)},
            output,
            colorPolicy);
    } else {
        replaced = runtime_->replaceAllText(*viewId, output, colorPolicy);
    }
    if (!replaced) {
        return std::make_error_code(std::errc::io_error);
    }

    searchSession_.match.reset();
    shell_.requestRefresh();
    scheduleSynchronize();
    return {};
}

std::error_code WxMainFrame::chooseSelectionTextColor() {
    const auto documentId = activeDocumentId();
    const auto viewId = activeView();
    auto* document = activeDocument();
    if (!documentId || !viewId || document == nullptr || runtime_ == nullptr ||
        !document->textBufferLoaded()) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    auto* host = runtime_->host(*viewId);
    if (host == nullptr) {
        synchronizeNow();
        host = runtime_->host(*viewId);
    }
    if (host == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    const auto runtime = host->runtime();
    const auto begin = std::min(runtime.caretOffset, runtime.anchorOffset);
    const auto end = std::max(runtime.caretOffset, runtime.anchorOffset);
    if (begin >= end) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    ensureTextAppearanceState(*documentId);
    auto& appearance = appearanceStates_[*documentId].appearance;
    const auto summary = summarizeSelectionAppearance(appearance, begin, end);
    wxColour initial = theme_.text;
    if (summary && !summary->foregroundArgb.mixed && summary->foregroundArgb.value) {
        const auto argb = *summary->foregroundArgb.value;
        initial = wxColour(static_cast<unsigned char>((argb >> 16U) & 0xFFU),
                           static_cast<unsigned char>((argb >> 8U) & 0xFFU),
                           static_cast<unsigned char>(argb & 0xFFU));
    }
    const wxString heading = summary && summary->foregroundArgb.mixed
                                 ? wxString("Mixed text colors. Choose a color to apply to all selected text.")
                                 : wxString("Choose a color for the selected text");

    WxTextColorDialog dialog(*this, theme_, initial, heading);
    if (dialog.ShowModal() != wxID_OK) {
        return {};
    }
    const auto color = dialog.color();
    auto candidate = appearance;
    const auto before = appearance.fragment(begin, end);
    const auto argb = 0xFF000000U |
                      (static_cast<std::uint32_t>(color.Red()) << 16U) |
                      (static_cast<std::uint32_t>(color.Green()) << 8U) |
                      static_cast<std::uint32_t>(color.Blue());
    candidate.setForeground(begin, end, argb);
    return commitSelectionAppearance(*documentId, *viewId, begin, end,
                                     std::move(candidate), before);
}

std::error_code WxMainFrame::clearSelectionTextColor() {
    const auto documentId = activeDocumentId();
    const auto viewId = activeView();
    if (!documentId || !viewId || runtime_ == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    auto* host = runtime_->host(*viewId);
    if (host == nullptr) {
        synchronizeNow();
        host = runtime_->host(*viewId);
    }
    if (host == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    const auto runtime = host->runtime();
    const auto begin = std::min(runtime.caretOffset, runtime.anchorOffset);
    const auto end = std::max(runtime.caretOffset, runtime.anchorOffset);
    if (begin >= end) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    ensureTextAppearanceState(*documentId);
    auto& appearance = appearanceStates_[*documentId].appearance;
    auto candidate = appearance;
    const auto before = appearance.fragment(begin, end);
    candidate.clearForeground(begin, end);
    return commitSelectionAppearance(*documentId, *viewId, begin, end,
                                     std::move(candidate), before);
}

std::error_code WxMainFrame::chooseSelectionFontFamily() {
    const auto documentId = activeDocumentId();
    const auto viewId = activeView();
    if (!documentId || !viewId || runtime_ == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    auto* host = runtime_->host(*viewId);
    if (host == nullptr) {
        synchronizeNow();
        host = runtime_->host(*viewId);
    }
    if (host == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    const auto runtime = host->runtime();
    const auto begin = std::min(runtime.caretOffset, runtime.anchorOffset);
    const auto end = std::max(runtime.caretOffset, runtime.anchorOffset);
    if (begin >= end) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    const auto refreshed = fontManager_.refresh();
    if (!refreshed) {
        return refreshed.error;
    }
    wxArrayString choices;
    for (const auto& family : fontManager_.families()) {
        choices.Add(fromUtf8(family));
    }
    if (choices.empty()) {
        return std::make_error_code(std::errc::no_such_device);
    }

    ensureTextAppearanceState(*documentId);
    auto& appearance = appearanceStates_[*documentId].appearance;
    int selection = wxNOT_FOUND;
    const auto summary = summarizeSelectionAppearance(appearance, begin, end);
    if (summary && !summary->fontFamilyId.mixed && summary->fontFamilyId.value) {
        const auto current = appearance.fontFamily(*summary->fontFamilyId.value);
        for (std::size_t index = 0; index < fontManager_.families().size(); ++index) {
            if (fontManager_.families()[index] == current) {
                selection = static_cast<int>(index);
                break;
            }
        }
    }

    const wxString familyPrompt = summary && summary->fontFamilyId.mixed
                                      ? wxString("Mixed fonts. Choose a font to apply to all selected text.")
                                      : wxString("Choose the font for the selected text");
    wxSingleChoiceDialog dialog(this, familyPrompt, "Selection Font Family", choices);
    if (selection != wxNOT_FOUND) {
        dialog.SetSelection(selection);
    }
    if (dialog.ShowModal() != wxID_OK) {
        return {};
    }

    auto candidate = appearance;
    const auto before = appearance.fragment(begin, end);
    const auto family = toUtf8(dialog.GetStringSelection());
    const auto familyId = candidate.internFontFamily(family);
    if (!familyId || !candidate.setFontFamily(begin, end, *familyId)) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    return commitSelectionAppearance(*documentId, *viewId, begin, end,
                                     std::move(candidate), before);
}

std::error_code WxMainFrame::clearSelectionFontFamily() {
    const auto documentId = activeDocumentId();
    const auto viewId = activeView();
    if (!documentId || !viewId || runtime_ == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    auto* host = runtime_->host(*viewId);
    if (host == nullptr) {
        synchronizeNow();
        host = runtime_->host(*viewId);
    }
    if (host == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    const auto runtime = host->runtime();
    const auto begin = std::min(runtime.caretOffset, runtime.anchorOffset);
    const auto end = std::max(runtime.caretOffset, runtime.anchorOffset);
    if (begin >= end) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    ensureTextAppearanceState(*documentId);
    auto& appearance = appearanceStates_[*documentId].appearance;
    auto candidate = appearance;
    const auto before = appearance.fragment(begin, end);
    candidate.clearFontFamily(begin, end);
    return commitSelectionAppearance(*documentId, *viewId, begin, end,
                                     std::move(candidate), before);
}

std::error_code WxMainFrame::chooseSelectionFontSize() {
    const auto documentId = activeDocumentId();
    const auto viewId = activeView();
    if (!documentId || !viewId || runtime_ == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    auto* host = runtime_->host(*viewId);
    if (host == nullptr) {
        synchronizeNow();
        host = runtime_->host(*viewId);
    }
    if (host == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    const auto runtime = host->runtime();
    const auto begin = std::min(runtime.caretOffset, runtime.anchorOffset);
    const auto end = std::max(runtime.caretOffset, runtime.anchorOffset);
    if (begin >= end) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    ensureTextAppearanceState(*documentId);
    auto& appearance = appearanceStates_[*documentId].appearance;
    const auto summary = summarizeSelectionAppearance(appearance, begin, end);
    const auto inheritedSize =
        static_cast<std::uint8_t>(std::clamp(settings_.fontPointSize, 5.0, 30.0));
    const long initial = summary && !summary->fontSizePoints.mixed && summary->fontSizePoints.value
                             ? static_cast<long>(*summary->fontSizePoints.value)
                             : static_cast<long>(inheritedSize);
    const wxString sizePrompt = summary && summary->fontSizePoints.mixed
                                    ? wxString("Mixed sizes. Choose a size to apply to all selected text.")
                                    : wxString("Choose the font size for the selected text");
    wxNumberEntryDialog dialog(this, sizePrompt,
                               "Size (pt)", "Selection Font Size", initial,
                               metadata::TextAppearanceMap::minimumFontSizePoints,
                               metadata::TextAppearanceMap::maximumFontSizePoints);
    if (dialog.ShowModal() != wxID_OK) {
        return {};
    }

    auto candidate = appearance;
    const auto before = appearance.fragment(begin, end);
    if (!candidate.setFontSize(begin, end, static_cast<std::uint8_t>(dialog.GetValue()))) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    return commitSelectionAppearance(*documentId, *viewId, begin, end,
                                     std::move(candidate), before);
}

std::error_code WxMainFrame::clearSelectionFontSize() {
    const auto documentId = activeDocumentId();
    const auto viewId = activeView();
    if (!documentId || !viewId || runtime_ == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    auto* host = runtime_->host(*viewId);
    if (host == nullptr) {
        synchronizeNow();
        host = runtime_->host(*viewId);
    }
    if (host == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    const auto runtime = host->runtime();
    const auto begin = std::min(runtime.caretOffset, runtime.anchorOffset);
    const auto end = std::max(runtime.caretOffset, runtime.anchorOffset);
    if (begin >= end) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    ensureTextAppearanceState(*documentId);
    auto& appearance = appearanceStates_[*documentId].appearance;
    auto candidate = appearance;
    const auto before = appearance.fragment(begin, end);
    candidate.clearFontSize(begin, end);
    return commitSelectionAppearance(*documentId, *viewId, begin, end,
                                     std::move(candidate), before);
}

std::error_code WxMainFrame::setSelectionSpoiler(const bool enabled) {
    const auto documentId = activeDocumentId();
    const auto viewId = activeView();
    if (!documentId || !viewId || runtime_ == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    auto* host = runtime_->host(*viewId);
    if (host == nullptr) {
        synchronizeNow();
        host = runtime_->host(*viewId);
    }
    if (host == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    const auto runtime = host->runtime();
    const auto begin = std::min(runtime.caretOffset, runtime.anchorOffset);
    const auto end = std::max(runtime.caretOffset, runtime.anchorOffset);
    if (begin >= end) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    ensureTextAppearanceState(*documentId);
    auto& appearance = appearanceStates_[*documentId].appearance;
    auto candidate = appearance;
    const auto before = appearance.fragment(begin, end);
    candidate.setSpoiler(begin, end, enabled);
    return commitSelectionAppearance(*documentId, *viewId, begin, end,
                                     std::move(candidate), before);
}

std::error_code WxMainFrame::resetSelectionAppearance() {
    const auto documentId = activeDocumentId();
    const auto viewId = activeView();
    if (!documentId || !viewId || runtime_ == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    auto* host = runtime_->host(*viewId);
    if (host == nullptr) {
        synchronizeNow();
        host = runtime_->host(*viewId);
    }
    if (host == nullptr) {
        return std::make_error_code(std::errc::operation_not_supported);
    }
    const auto runtime = host->runtime();
    const auto begin = std::min(runtime.caretOffset, runtime.anchorOffset);
    const auto end = std::max(runtime.caretOffset, runtime.anchorOffset);
    if (begin >= end) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    ensureTextAppearanceState(*documentId);
    auto& appearance = appearanceStates_[*documentId].appearance;
    auto candidate = appearance;
    const auto before = appearance.fragment(begin, end);
    candidate.reset(begin, end);
    return commitSelectionAppearance(*documentId, *viewId, begin, end,
                                     std::move(candidate), before);
}

std::error_code WxMainFrame::commitSelectionAppearance(
    const core::DocumentId documentId,
    const workspace::ViewId viewId,
    const std::uint64_t begin,
    const std::uint64_t end,
    metadata::TextAppearanceMap candidate,
    const std::span<const metadata::TextAppearanceSpan> before) {
    if (!appearanceFitsStyleBudget(candidate, 128U)) {
        wxMessageBox("This document already uses the maximum 128 unique font/color/size "
                     "combinations. Remove or reuse an existing selection appearance first.",
                     "Selection Appearance", wxOK | wxICON_WARNING, this);
        return std::make_error_code(std::errc::no_buffer_space);
    }

    auto iterator = appearanceStates_.find(documentId);
    if (iterator == appearanceStates_.end()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    const auto after = candidate.fragment(begin, end);
    if (before.size() == after.size() &&
        std::equal(before.begin(), before.end(), after.begin())) {
        return {};
    }

    auto previous = iterator->second.appearance;
    const auto previousRevision = iterator->second.revision;
    iterator->second.appearance = std::move(candidate);
    ++iterator->second.revision;
    if (const auto error = persistTextAppearance(documentId)) {
        iterator->second.appearance = std::move(previous);
        iterator->second.revision = previousRevision;
        return error;
    }

    checkpointTextAppearance(documentId);
    if (runtime_ != nullptr) {
        if (auto* host = runtime_->host(viewId); host != nullptr) {
            static_cast<void>(host->recordTextAppearanceUndo(begin, end, before, after));
        }
    }
    shell_.requestRefresh();
    scheduleSynchronize();
    return {};
}

void WxMainFrame::ensureTextAppearanceState(const core::DocumentId documentId) {
    auto& state = appearanceStates_[documentId];
    if (state.loaded) {
        return;
    }

    const auto* document = documents_.get(documentId);
    if (document == nullptr || !document->textBufferLoaded()) {
        return;
    }
    state.loaded = true;
    if (document->path().empty()) {
        return;
    }

    const auto metadataPath = metadataStore_.metadataPath(document->path());
    std::error_code metadataExistsError;
    if (!std::filesystem::is_regular_file(metadataPath, metadataExistsError)) {
        return;
    }

    const auto loaded = metadataStore_.load(document->path(), document->text());
    if (!loaded) {
        return;
    }
    if (loaded.stale) {
        return;
    }
    state.appearance = loaded.metadata.appearance;
    ++state.revision;
}

EditorHostAppearance WxMainFrame::appearanceForDocument(const core::DocumentId documentId) {
    ensureTextAppearanceState(documentId);
    const auto iterator = appearanceStates_.find(documentId);
    if (iterator == appearanceStates_.end()) {
        return {};
    }
    return {iterator->second.appearance.spans(),
            iterator->second.appearance.fontFamilies(),
            iterator->second.revision,
            iterator->second.appearance.hasSpoilers()};
}

void WxMainFrame::noteTextAppearanceEdit(const core::DocumentId documentId,
                                         const EditorTextEdit& edit) {
    ensureTextAppearanceState(documentId);
    auto iterator = appearanceStates_.find(documentId);
    if (iterator == appearanceStates_.end() || iterator->second.appearance.empty() ||
        edit.colorPolicy == metadata::AppearanceEditPolicy::PreserveState) {
        return;
    }
    iterator->second.appearance.applyEdit(
        edit.offset, edit.eraseBytes, edit.insertedText.size(), edit.colorPolicy);
    ++iterator->second.revision;
}

bool WxMainFrame::restoreTextAppearanceRange(
    const core::DocumentId documentId,
    const std::uint64_t begin,
    const std::uint64_t end,
    const std::span<const metadata::TextAppearanceSpan> spans) {
    ensureTextAppearanceState(documentId);
    auto iterator = appearanceStates_.find(documentId);
    if (iterator == appearanceStates_.end()) {
        return false;
    }

    std::vector<metadata::TextAppearanceSpan> restored(spans.begin(), spans.end());
    if (!iterator->second.appearance.replaceRange(begin, end, std::move(restored))) {
        return false;
    }
    ++iterator->second.revision;
    static_cast<void>(persistTextAppearance(documentId));
    checkpointTextAppearance(documentId);
    shell_.requestRefresh();
    scheduleSynchronize();
    return true;
}

void WxMainFrame::checkpointTextAppearance(const core::DocumentId documentId) {
    const auto iterator = appearanceStates_.find(documentId);
    const auto* document = documents_.get(documentId);
    if (iterator == appearanceStates_.end() || !iterator->second.loaded ||
        document == nullptr || !document->modified() || !document->textBufferLoaded()) {
        return;
    }
    if (lifecycle_.recovery().checkpoint(
            documentId, *document, &iterator->second.appearance)) {
        return;
    }
    static_cast<void>(lifecycle_.saveSession());
}

std::error_code WxMainFrame::persistTextAppearance(const core::DocumentId documentId) {
    const auto iterator = appearanceStates_.find(documentId);
    const auto* document = documents_.get(documentId);
    if (iterator == appearanceStates_.end() || !iterator->second.loaded || document == nullptr ||
        document->path().empty() || !document->textBufferLoaded()) {
        return {};
    }
    if (iterator->second.appearance.empty()) {
        return metadataStore_.erase(document->path());
    }
    return metadataStore_.save(document->path(), document->text(), iterator->second.appearance);
}

void WxMainFrame::resetTextAppearance(const core::DocumentId documentId) {
    appearanceStates_.erase(documentId);
}

std::error_code WxMainFrame::editorCommand(const EditorHostCommand command) {
    const auto viewId = activeView();
    if (!viewId || runtime_ == nullptr || !runtime_->executeEditorCommand(*viewId, command)) {
        return std::make_error_code(std::errc::operation_not_permitted);
    }
    return {};
}

std::error_code WxMainFrame::showSearchDialog(const bool replaceMode) {
    if (searchDialog_ == nullptr) {
        searchDialog_ = new WxSearchDialog(
            *this,
            theme_,
            [this](const WxSearchDialog::Action action,
                   const WxSearchDialog::Values& values) {
                const bool queryChanged = values.pattern != searchSession_.pattern ||
                                          !sameSearchOptions(values.options,
                                                             searchSession_.options);
                searchSession_.pattern = values.pattern;
                searchSession_.replacement = values.replacement;
                searchSession_.options = values.options;
                if (queryChanged) {
                    searchSession_.match.reset();
                }

                switch (action) {
                case WxSearchDialog::Action::Previous:
                    static_cast<void>(findFromSession(search::SearchDirection::Backward));
                    break;
                case WxSearchDialog::Action::Next:
                    static_cast<void>(findFromSession(search::SearchDirection::Forward));
                    break;
                case WxSearchDialog::Action::Replace:
                    static_cast<void>(replaceCurrentMatch());
                    break;
                case WxSearchDialog::Action::ReplaceAll:
                    static_cast<void>(replaceAllMatches());
                    break;
                }
            });
    }

    searchDialog_->setReplaceMode(replaceMode);
    searchDialog_->setValues(searchSession_.pattern,
                             searchSession_.replacement,
                             searchSession_.options);
    searchDialog_->setStatus("Ready");
    searchDialog_->Show();
    searchDialog_->Raise();
    searchDialog_->focusFind();
    return {};
}

std::error_code WxMainFrame::findFromSession(const search::SearchDirection direction) {
    if (searchSession_.pattern.empty()) {
        return showSearchDialog(false);
    }

    const auto viewId = activeView();
    const auto documentId = activeDocumentId();
    auto* document = activeDocument();
    if (!viewId || !documentId || document == nullptr || runtime_ == nullptr) {
        setSearchStatus("No active document.");
        return {};
    }

    const auto* persistedView = workspace_.view(*viewId);
    std::uint64_t startOffset = persistedView == nullptr
                                    ? 0U
                                    : static_cast<std::uint64_t>(persistedView->caretOffset);
    if (const auto* host = runtime_->host(*viewId)) {
        startOffset = host->runtime().caretOffset;
    }
    const bool currentMatchValid = searchSession_.match.has_value() &&
                                   searchSession_.document == *documentId &&
                                   searchSession_.revision == document->revision();
    if (currentMatchValid) {
        const auto match = *searchSession_.match;
        if (direction == search::SearchDirection::Forward) {
            if (match.length == 0U && document->textBufferLoaded()) {
                startOffset = nextUtf8Boundary(document->text(), match.offset);
            } else if (match.length <= std::numeric_limits<std::uint64_t>::max() - match.offset) {
                startOffset = match.offset + match.length;
            } else {
                startOffset = match.offset;
            }
        } else {
            startOffset = match.offset;
        }
    }

    EditorHostSearchRequest request;
    request.pattern = searchSession_.pattern;
    request.options = searchSession_.options;
    request.direction = direction;
    request.startOffset = startOffset;

    const auto found = runtime_->findText(*viewId, request);
    if (!found) {
        if (found.error == std::errc::operation_not_supported) {
            setSearchStatus(
                "This search mode or encoding is not supported in scalable View Mode yet. "
                "Use Edit Anyway for regex/unsupported encodings.");
        } else if (found.error == std::errc::invalid_argument) {
            setSearchStatus("Invalid search pattern.");
        } else if (found.error == std::errc::operation_canceled) {
            setSearchStatus("Search cancelled.");
        } else {
            setSearchStatus("Search failed: " + found.error.message());
        }
        return {};
    }

    if (!found.match) {
        searchSession_.match.reset();
        searchSession_.document = *documentId;
        searchSession_.revision = document->revision();
        setSearchStatus("No matches.");
        return {};
    }

    searchSession_.match = found.match;
    searchSession_.document = *documentId;
    searchSession_.revision = document->revision();

    std::string status = "Match at byte " + std::to_string(found.match->offset);
    if (found.wrapped) {
        status += " (wrapped)";
    }
    if (found.bytesScanned != 0U) {
        status += " | scanned " + std::to_string(found.bytesScanned) + " B";
    }
    setSearchStatus(status);
    return {};
}

std::error_code WxMainFrame::replaceCurrentMatch() {
    const auto viewId = activeView();
    const auto documentId = activeDocumentId();
    auto* document = activeDocument();
    if (!viewId || !documentId || document == nullptr) {
        setSearchStatus("No active document.");
        return {};
    }

    const auto* runtimeState = presentation_.viewRuntime(*viewId);
    if (runtimeState == nullptr || runtimeState->openMode != core::OpenMode::Editor ||
        !document->textBufferLoaded()) {
        setSearchStatus("Replace is available only in Editor mode.");
        return {};
    }
    if (searchSession_.pattern.empty()) {
        setSearchStatus("Enter text to find first.");
        return {};
    }

    bool currentMatchValid = searchSession_.match.has_value() &&
                             searchSession_.document == *documentId &&
                             searchSession_.revision == document->revision();
    if (!currentMatchValid) {
        static_cast<void>(findFromSession(search::SearchDirection::Forward));
        currentMatchValid = searchSession_.match.has_value() &&
                            searchSession_.document == *documentId &&
                            searchSession_.revision == document->revision();
        if (!currentMatchValid) {
            return {};
        }
    }

    const auto selected = *searchSession_.match;
    auto replaced = search::TextSearch::replaceMatch(
        document->text(),
        searchSession_.pattern,
        searchSession_.replacement,
        selected,
        searchSession_.options);
    if (!replaced) {
        searchSession_.match.reset();
        setSearchStatus(replaced.error == std::errc::invalid_argument
                            ? "The current match changed. Find it again."
                            : "Replace failed: " + replaced.error.message());
        return {};
    }

    const auto replacementOffset = static_cast<std::size_t>(selected.offset);
    const auto replacementLength = static_cast<std::size_t>(replaced.replacementLength);
    const std::string_view replacementText(replaced.text.data() + replacementOffset,
                                           replacementLength);
    if (runtime_ == nullptr ||
        !runtime_->replaceTextRange(*viewId, selected, replacementText)) {
        setSearchStatus("Replace could not be applied to the active editor.");
        return {};
    }

    searchSession_.match.reset();
    searchSession_.document = *documentId;
    searchSession_.revision = document->revision();
    shell_.requestRefresh();
    scheduleSynchronize();
    setSearchStatus("Replaced 1 match.");
    return {};
}

std::error_code WxMainFrame::replaceAllMatches() {
    const auto viewId = activeView();
    const auto documentId = activeDocumentId();
    auto* document = activeDocument();
    if (!viewId || !documentId || document == nullptr) {
        setSearchStatus("No active document.");
        return {};
    }

    const auto* runtimeState = presentation_.viewRuntime(*viewId);
    if (runtimeState == nullptr || runtimeState->openMode != core::OpenMode::Editor ||
        !document->textBufferLoaded()) {
        setSearchStatus("Replace All is available only in Editor mode.");
        return {};
    }
    if (searchSession_.pattern.empty()) {
        setSearchStatus("Enter text to find first.");
        return {};
    }

    auto options = searchSession_.options;
    auto replaced = search::TextSearch::replaceAll(
        document->text(), searchSession_.pattern, searchSession_.replacement, options);
    if (!replaced) {
        setSearchStatus(replaced.error == std::errc::invalid_argument
                            ? "Invalid search pattern."
                            : "Replace All failed: " + replaced.error.message());
        return {};
    }
    if (replaced.replacements == 0U) {
        setSearchStatus("No matches.");
        return {};
    }
    if (replaced.truncated) {
        setSearchStatus("Replace All stopped at the safety result limit; no changes were made.");
        return {};
    }
    if (runtime_ == nullptr || !runtime_->replaceAllText(*viewId, replaced.text)) {
        setSearchStatus("Replace All could not be applied to the active editor.");
        return {};
    }

    searchSession_.match.reset();
    searchSession_.document = *documentId;
    searchSession_.revision = document->revision();
    shell_.requestRefresh();
    scheduleSynchronize();

    setSearchStatus("Replaced " + std::to_string(replaced.replacements) + " matches.");
    return {};
}

std::error_code WxMainFrame::showGoToLineDialog() {
    const auto viewId = activeView();
    if (!viewId || runtime_ == nullptr) {
        return {};
    }

    std::uint64_t currentLine = 1U;
    if (const auto* state = presentation_.viewRuntime(*viewId)) {
        currentLine = std::max<std::uint64_t>(state->caretLine, 1U);
    }
    wxTextEntryDialog dialog(
        this,
        "Enter a 1-based line number:",
        "Go To Line",
        wxString::Format("%llu", static_cast<unsigned long long>(currentLine)));
    if (dialog.ShowModal() != wxID_OK) {
        return {};
    }

    const auto entered = toUtf8(dialog.GetValue());
    std::uint64_t parsed = 0U;
    const auto* begin = entered.data();
    const auto* end = begin + entered.size();
    const auto [parsedEnd, parseError] = std::from_chars(begin, end, parsed);
    if (parseError != std::errc{} || parsedEnd != end || parsed == 0U) {
        wxMessageBox("Enter a valid line number greater than zero.",
                     "Go To Line", wxOK | wxICON_WARNING, this);
        return {};
    }

    const auto error = runtime_->goToLine(*viewId, parsed);
    if (error == std::errc::result_out_of_range) {
        wxMessageBox("That line is outside the document.",
                     "Go To Line", wxOK | wxICON_INFORMATION, this);
        return {};
    }
    if (error) {
        showError("Could not go to line", error);
    }
    scheduleSynchronize();
    return {};
}

void WxMainFrame::setSearchStatus(const std::string& status) {
    if (searchDialog_ != nullptr) {
        searchDialog_->setStatus(fromUtf8(status));
    }
}

bool WxMainFrame::openPath(const std::filesystem::path& path) {
    if (path.empty()) {
        return false;
    }

    std::optional<workspace::ViewId> disposableView;
    core::DocumentId disposableDocument{};
    if (workspace_.viewCount() == 1U) {
        const auto current = activeView();
        const auto* viewState = current ? workspace_.view(*current) : nullptr;
        const auto* document = viewState == nullptr ? nullptr : documents_.get(viewState->document);
        if (current && viewState != nullptr && document != nullptr && document->path().empty() &&
            !document->modified() && document->text().empty()) {
            disposableView = current;
            disposableDocument = viewState->document;
        }
    }

    const auto opened = documents_.openRouted(path, settings_.inspectOptions);
    if (!opened) {
        showError("Could not open file", opened.error);
        return false;
    }

    if (disposableView) {
        static_cast<void>(shell_.closeView(*disposableView));
        static_cast<void>(documents_.close(disposableDocument));
    }

    std::optional<workspace::ViewId> existingView;
    workspace_.forEachView([&existingView, document = opened.id](const workspace::ViewState& view) {
        if (!existingView && view.document == document) existingView = view.id;
    });
    if (existingView && shell_.activateView(*existingView)) {
        const auto* document = documents_.get(opened.id);
        if (document != nullptr) {
            static_cast<void>(lifecycle_.noteOpened(path, document->profile().recommendedMode));
        }
        static_cast<void>(lifecycle_.saveSession());
        scheduleSynchronize();
        return true;
    }

    const auto pane = workspace_.activePane() ? workspace_.activePane() : workspace_.primaryPane();
    const auto view = workspace_.openView(opened.id, pane);
    if (!view) {
        if (!opened.reusedExisting) {
            static_cast<void>(documents_.close(opened.id));
        }
        return false;
    }
    static_cast<void>(workspace_.setActiveView(pane, view));
    static_cast<void>(workspace_.setActivePane(pane));
    if (const auto* document = documents_.get(opened.id); document != nullptr) {
        static_cast<void>(lifecycle_.noteOpened(path, document->profile().recommendedMode));
    }
    static_cast<void>(lifecycle_.saveSession());
    shell_.requestRefresh();
    scheduleSynchronize();
    return true;
}

bool WxMainFrame::openPathInPane(const std::filesystem::path& path,
                                     const workspace::PaneId pane) {
    if (path.empty() || workspace_.pane(pane) == nullptr) {
        return false;
    }

    std::optional<workspace::ViewId> disposableView;
    core::DocumentId disposableDocument{};
    if (workspace_.viewCount() == 1U) {
        const auto current = activeView();
        const auto* viewState = current ? workspace_.view(*current) : nullptr;
        const auto* document = viewState == nullptr ? nullptr : documents_.get(viewState->document);
        const auto containing = current ? workspace_.paneContaining(*current) : std::nullopt;
        if (current && containing && *containing == pane && viewState != nullptr &&
            document != nullptr && document->path().empty() && !document->modified() &&
            document->text().empty()) {
            disposableView = current;
            disposableDocument = viewState->document;
        }
    }

    const auto opened = documents_.openRouted(path, settings_.inspectOptions);
    if (!opened) {
        showError("Could not open file", opened.error);
        return false;
    }

    const auto targetView = shell_.openDocumentInPane(opened.id, pane);
    if (!targetView) {
        if (!opened.reusedExisting) {
            static_cast<void>(documents_.close(opened.id));
        }
        return false;
    }

    if (disposableView && *disposableView != targetView) {
        static_cast<void>(shell_.closeView(*disposableView));
        static_cast<void>(documents_.close(disposableDocument));
    }

    if (const auto* document = documents_.get(opened.id); document != nullptr) {
        static_cast<void>(lifecycle_.noteOpened(path, document->profile().recommendedMode));
    }
    static_cast<void>(lifecycle_.saveSession());
    shell_.requestRefresh();
    scheduleSynchronize();
    return true;
}

bool WxMainFrame::openDroppedFiles(
    const std::vector<std::filesystem::path>& paths,
    const std::optional<workspace::PaneId> targetPane) {
    const auto prepared = nff::gui::prepareFileDropPaths(paths);
    if (prepared.files.empty()) {
        return false;
    }

    auto pane = workspace_.activePane() ? workspace_.activePane() : workspace_.primaryPane();
    if (targetPane && workspace_.pane(*targetPane) != nullptr) {
        pane = *targetPane;
    }

    bool openedAny = false;
    for (const auto& path : prepared.files) {
        openedAny = openPathInPane(path, pane) || openedAny;
    }
    return openedAny;
}

std::error_code WxMainFrame::startSidebarIndex(const std::filesystem::path& root) {
    stopSidebarSearch();
    stopSidebarIndex();

    const auto normalizedRoot = root.lexically_normal();
    const bool sameRoot = !sidebarRoot_.empty() && sidebarRoot_ == normalizedRoot;
    sidebarRoot_ = normalizedRoot;
    sidebarScope_ = sidebarRoot_;
    sidebarRootUnavailable_ = false;
    sidebarIndexedFiles_ = 0U;
    sidebarIndexTruncated_ = false;
    sidebarIndexAvailable_ = false;
    sidebarIndexReady_.store(false, std::memory_order_relaxed);
    fileSearch_.clear();
    presentation_.setSidebarFolderActive(true);

    if (!settings_.sidebarVisible) {
        sidebarIndexing_ = false;
        return {};
    }

    if (!sameRoot) {
        resetSidebarTree();
    }
    sidebarIndexing_ = true;
    updateSidebarHint();
    shell_.requestRefresh();
    scheduleSynchronize();

    const auto selectedRoot = sidebarRoot_;
    try {
        sidebarIndexThread_ = std::jthread([this, selectedRoot](const std::stop_token stopToken) {
            try {
                search::FileSearchBuildOptions options;
                options.includeDirectories = false;

                options.followDirectorySymlinks = false;
                const std::array roots{selectedRoot};
                sidebarIndexResult_ = fileSearch_.rebuild(
                    roots, options, [&stopToken]() { return stopToken.stop_requested(); });
            } catch (const std::bad_alloc&) {
                sidebarIndexResult_ = {};
                sidebarIndexResult_.error = std::make_error_code(std::errc::not_enough_memory);
            } catch (const std::filesystem::filesystem_error& error) {
                sidebarIndexResult_ = {};
                sidebarIndexResult_.error = error.code()
                    ? error.code()
                    : std::make_error_code(std::errc::io_error);
            } catch (const std::system_error& error) {
                sidebarIndexResult_ = {};
                sidebarIndexResult_.error = error.code()
                    ? error.code()
                    : std::make_error_code(std::errc::io_error);
            } catch (...) {
                sidebarIndexResult_ = {};
                sidebarIndexResult_.error = std::make_error_code(std::errc::io_error);
            }
            sidebarIndexReady_.store(true, std::memory_order_release);
        });
    } catch (const std::system_error& error) {
        sidebarIndexing_ = false;
        updateSidebarHint();
        return error.code() ? error.code() : std::make_error_code(std::errc::resource_unavailable_try_again);
    }
    return {};
}

void WxMainFrame::stopSidebarIndex() noexcept {
    if (sidebarIndexThread_.joinable()) {
        sidebarIndexThread_.request_stop();
        sidebarIndexThread_.join();
    }
    sidebarIndexReady_.store(false, std::memory_order_relaxed);
    sidebarIndexing_ = false;
}

void WxMainFrame::processSidebarIndexCompletion() {
    if (!sidebarIndexReady_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    if (sidebarIndexThread_.joinable()) {
        sidebarIndexThread_.join();
    }

    sidebarIndexing_ = false;
    const auto result = sidebarIndexResult_;
    if (result.error == std::make_error_code(std::errc::operation_canceled)) {
        updateSidebarHint();
        return;
    }
    if (result.error) {
        showError("Could not index search folder", result.error);
        updateSidebarHint();
        return;
    }

    sidebarIndexedFiles_ = fileSearch_.size();
    sidebarIndexTruncated_ = result.stats.truncated;
    sidebarIndexAvailable_ = true;
    refreshSidebarTreePreservingState();
    updateSidebarHint();
    if (!sidebarSearchQuery_.empty()) {
        requestSidebarSearch(sidebarSearchQuery_);
    }
    shell_.requestRefresh();
    scheduleSynchronize();
}

void WxMainFrame::updateSidebarHint() {
    if (sidebarSearch_ == nullptr) {
        return;
    }
    if (sidebarIndexing_) {
        sidebarSearch_->SetHint("Indexing folder...");
        return;
    }
    if (sidebarRootUnavailable_) {
        sidebarSearch_->SetHint("Folder unavailable - use Refresh when it returns...");
        return;
    }
    if (!sidebarRoot_.empty()) {
        const auto scope = sidebarSearchScope();
        auto scopeName = pathUtf8(scope.filename());
        if (scopeName.empty()) {
            scopeName = pathUtf8(scope);
        }
        if (!sidebarIndexAvailable_) {
            sidebarSearch_->SetHint(wxString("Search files in ") + fromUtf8(scopeName) +
                                    " (F5 to index)");
            return;
        }
        wxString hint = wxString("Search ") + fromUtf8(scopeName);
        hint += wxString::Format(" (%llu indexed",
                                 static_cast<unsigned long long>(sidebarIndexedFiles_));
        if (sidebarIndexTruncated_) {
            hint += ", limit reached";
        }
        hint += ")";
        sidebarSearch_->SetHint(hint);
        return;
    }
    sidebarSearch_->SetHint("Choose a folder to search...");
}

void WxMainFrame::resetSidebarTree() {
    if (sidebarTree_ == nullptr) {
        return;
    }

    sidebarTree_->DeleteAllItems();
    sidebarSearchTreeMode_ = false;
    sidebarQueryCache_.clear();
    sidebarScopeCache_.clear();

    if (sidebarRoot_.empty()) {
        sidebarTree_->AddRoot("No folder selected");
        return;
    }

    sidebarScope_ = pathWithinScope(sidebarScope_, sidebarRoot_) ? sidebarScope_ : sidebarRoot_;
    auto label = pathUtf8(sidebarRoot_.filename());
    if (label.empty()) {
        label = pathUtf8(sidebarRoot_);
    }

    const auto root = sidebarTree_->AddRoot(
        fromUtf8(label), -1, -1,
        new SidebarTreeItemData(sidebarRoot_, true, false));
    populateSidebarDirectory(root, sidebarRoot_);
    sidebarTree_->SelectItem(root);
    sidebarTree_->Expand(root);
}

void WxMainFrame::refreshSidebarTreePreservingState() {
    if (sidebarTree_ == nullptr || sidebarSearchTreeMode_) {
        return;
    }

    std::vector<std::filesystem::path> expanded;
    const auto root = sidebarTree_->GetRootItem();
    if (root.IsOk()) {
        collectExpandedSidebarPaths(root, expanded);
    }

    std::filesystem::path selectedPath;
    if (const auto selected = sidebarTree_->GetSelection(); selected.IsOk()) {
        if (const auto* data = sidebarTreeData(sidebarTree_, selected); data != nullptr) {
            selectedPath = data->path;
        }
    }
    const auto savedScope = sidebarScope_;

    sidebarTreeRefreshing_ = true;
    resetSidebarTree();
    std::ranges::sort(expanded, [](const auto& left, const auto& right) {
        return std::distance(left.begin(), left.end()) < std::distance(right.begin(), right.end());
    });
    for (const auto& path : expanded) {
        static_cast<void>(expandSidebarPath(path));
    }
    if (!selectedPath.empty()) {
        const auto selected = findSidebarTreeItem(selectedPath);
        if (selected.IsOk()) {
            sidebarTree_->SelectItem(selected);
            sidebarTree_->EnsureVisible(selected);
        }
    }
    sidebarScope_ = pathWithinScope(savedScope, sidebarRoot_) ? savedScope : sidebarRoot_;
    sidebarTreeRefreshing_ = false;
}

void WxMainFrame::collectExpandedSidebarPaths(
    const wxTreeItemId& item,
    std::vector<std::filesystem::path>& paths) const {
    if (sidebarTree_ == nullptr || !item.IsOk()) {
        return;
    }
    const auto* data = sidebarTreeData(sidebarTree_, item);
    if (data != nullptr && data->directory && sidebarTree_->IsExpanded(item)) {
        paths.push_back(data->path);
    }

    wxTreeItemIdValue cookie;
    auto child = sidebarTree_->GetFirstChild(item, cookie);
    while (child.IsOk()) {
        collectExpandedSidebarPaths(child, paths);
        child = sidebarTree_->GetNextChild(item, cookie);
    }
}

wxTreeItemId WxMainFrame::findSidebarChild(const wxTreeItemId& parent,
                                           const std::filesystem::path& path) const {
    if (sidebarTree_ == nullptr || !parent.IsOk()) {
        return {};
    }
    const auto normalized = path.lexically_normal();
    wxTreeItemIdValue cookie;
    auto child = sidebarTree_->GetFirstChild(parent, cookie);
    while (child.IsOk()) {
        const auto* data = sidebarTreeData(sidebarTree_, child);
        if (data != nullptr && data->path.lexically_normal() == normalized) {
            return child;
        }
        child = sidebarTree_->GetNextChild(parent, cookie);
    }
    return {};
}

wxTreeItemId WxMainFrame::expandSidebarPath(const std::filesystem::path& path) {
    if (sidebarTree_ == nullptr || sidebarRoot_.empty() ||
        !pathWithinScope(path, sidebarRoot_)) {
        return {};
    }

    auto currentItem = sidebarTree_->GetRootItem();
    if (!currentItem.IsOk()) {
        return {};
    }
    if (path.lexically_normal() == sidebarRoot_.lexically_normal()) {
        sidebarTree_->Expand(currentItem);
        return currentItem;
    }

    auto currentPath = sidebarRoot_;
    const auto relative = path.lexically_normal().lexically_relative(sidebarRoot_.lexically_normal());
    for (const auto& component : relative) {
        auto* data = sidebarTreeData(sidebarTree_, currentItem);
        if (data != nullptr && data->directory && !data->loaded) {
            populateSidebarDirectory(currentItem, data->path);
        }
        currentPath /= component;
        const auto child = findSidebarChild(currentItem, currentPath);
        if (!child.IsOk()) {
            return {};
        }
        currentItem = child;
        const auto* childData = sidebarTreeData(sidebarTree_, currentItem);
        if (childData != nullptr && childData->directory) {
            sidebarTree_->Expand(currentItem);
        }
    }
    return currentItem;
}

wxTreeItemId WxMainFrame::findSidebarTreeItem(const std::filesystem::path& path) {
    if (sidebarTree_ == nullptr || sidebarRoot_.empty() ||
        !pathWithinScope(path, sidebarRoot_)) {
        return {};
    }
    if (path.lexically_normal() == sidebarRoot_.lexically_normal()) {
        return sidebarTree_->GetRootItem();
    }
    const auto parentPath = path.parent_path();
    const auto parent = expandSidebarPath(parentPath);
    return findSidebarChild(parent, path);
}

void WxMainFrame::populateSidebarDirectory(const wxTreeItemId& item,
                                           const std::filesystem::path& directory) {
    if (sidebarTree_ == nullptr || !item.IsOk()) {
        return;
    }

    auto* parentData = sidebarTreeData(sidebarTree_, item);
    if (parentData == nullptr || !parentData->directory || parentData->loaded) {
        return;
    }

    struct Child final {
        std::filesystem::path path;
        bool directory{false};
    };
    std::vector<Child> children;
    std::error_code error;
    std::filesystem::directory_iterator iterator(
        directory, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end) {
        const auto path = iterator->path();
        std::error_code typeError;
        const bool isDirectory = iterator->is_directory(typeError);
        if (!typeError && !sidebarHiddenName(path) &&
            (!isDirectory || !sidebarExcludedDirectory(path))) {
            if (isDirectory) {
                children.push_back({path, true});
            } else if (iterator->is_regular_file(typeError) && !typeError) {
                children.push_back({path, false});
            }
        }
        iterator.increment(error);
    }

    std::ranges::sort(children, [](const Child& left, const Child& right) {
        if (left.directory != right.directory) {
            return left.directory > right.directory;
        }
        return sidebarSortKey(left.path) < sidebarSortKey(right.path);
    });

    sidebarTree_->DeleteChildren(item);
    for (const auto& child : children) {
        auto name = pathUtf8(child.path.filename());
        if (name.empty()) {
            name = pathUtf8(child.path);
        }
        const auto childItem = sidebarTree_->AppendItem(
            item, fromUtf8(name), -1, -1,
            new SidebarTreeItemData(child.path, child.directory, !child.directory));
        if (child.directory) {
            sidebarTree_->AppendItem(childItem, "...");
        }
    }
    parentData->loaded = true;
}

std::filesystem::path WxMainFrame::sidebarSearchScope() const {

    return sidebarRoot_;
}

void WxMainFrame::requestSidebarSearch(std::string query) {
    if (query.empty()) {
        stopSidebarSearch();
        refreshSidebarSearchNow({});
        sidebarGenerationCache_ = fileSearch_.generation();
        sidebarQueryCache_.clear();
        sidebarScopeCache_ = pathUtf8(sidebarSearchScope());
        return;
    }

    if (!sidebarIndexAvailable_ || sidebarIndexing_) {
        stopSidebarSearch();
        refreshSidebarSearchNow(query);
        return;
    }

    ensureSidebarSearchWorker();
    {
        std::lock_guard lock(sidebarSearchMutex_);
        sidebarPendingSearchQuery_ = std::move(query);
        ++sidebarSearchRequestSerial_;
    }
    sidebarSearchCv_.notify_one();

    if (!sidebarSearchTimer_.IsRunning()) {
        sidebarSearchTimer_.Start(25);
    }
}

void WxMainFrame::ensureSidebarSearchWorker() {
    if (sidebarSearchThread_.joinable()) {
        return;
    }

    sidebarSearchThread_ = std::jthread([this](const std::stop_token stopToken) {
        std::uint64_t processedSerial = 0U;
        while (!stopToken.stop_requested()) {
            std::string query;
            std::uint64_t serial = 0U;
            {
                std::unique_lock lock(sidebarSearchMutex_);
                const bool ready = sidebarSearchCv_.wait(
                    lock, stopToken, [this, processedSerial] {
                        return sidebarSearchRequestSerial_.load(std::memory_order_relaxed) !=
                               processedSerial;
                    });
                if (!ready || stopToken.stop_requested()) {
                    return;
                }
                serial = sidebarSearchRequestSerial_.load(std::memory_order_relaxed);
                query = sidebarPendingSearchQuery_;
            }

            auto hits = fileSearch_.search(
                query, 100U, [this, serial, &stopToken]() {
                    return stopToken.stop_requested() ||
                           sidebarSearchRequestSerial_.load(std::memory_order_relaxed) != serial;
                });

            {
                std::lock_guard lock(sidebarSearchMutex_);
                if (serial == sidebarSearchRequestSerial_.load(std::memory_order_relaxed)) {
                    sidebarCompletedSearchQuery_ = std::move(query);
                    sidebarCompletedSearchHits_ = std::move(hits);
                    sidebarSearchCompletedSerial_ = serial;
                }
            }
            processedSerial = serial;
        }
    });
}

void WxMainFrame::stopSidebarSearch() noexcept {
    sidebarSearchTimer_.Stop();
    if (sidebarSearchThread_.joinable()) {
        sidebarSearchThread_.request_stop();
        sidebarSearchCv_.notify_all();
        sidebarSearchThread_.join();
    }
    std::lock_guard lock(sidebarSearchMutex_);
    sidebarPendingSearchQuery_.clear();
    sidebarCompletedSearchQuery_.clear();
    sidebarCompletedSearchHits_.clear();
    sidebarSearchRequestSerial_ = 0U;
    sidebarSearchCompletedSerial_ = 0U;
    sidebarSearchAppliedSerial_ = 0U;
}

void WxMainFrame::processSidebarSearchCompletion() {
    std::string query;
    std::vector<search::FileSearchHit> hits;
    bool completed = false;
    bool idle = false;
    {
        std::lock_guard lock(sidebarSearchMutex_);
        if (sidebarSearchCompletedSerial_ > sidebarSearchAppliedSerial_ &&
            sidebarSearchCompletedSerial_ ==
                sidebarSearchRequestSerial_.load(std::memory_order_relaxed)) {
            query = sidebarCompletedSearchQuery_;
            hits = std::move(sidebarCompletedSearchHits_);
            sidebarSearchAppliedSerial_ = sidebarSearchCompletedSerial_;
            completed = true;
        }
        idle = sidebarSearchAppliedSerial_ ==
               sidebarSearchRequestSerial_.load(std::memory_order_relaxed);
    }

    if (completed && query == sidebarSearchQuery_ &&
        sidebarIndexAvailable_ && !sidebarIndexing_) {
        rebuildSidebarSearchTree(query, hits);
        sidebarGenerationCache_ = fileSearch_.generation();
        sidebarQueryCache_ = query;
        sidebarScopeCache_ = pathUtf8(sidebarSearchScope());
    }

    if (idle) {
        sidebarSearchTimer_.Stop();
    }
}

void WxMainFrame::refreshSidebarSearchNow(const std::string_view query) {
    if (sidebarTree_ == nullptr) {
        return;
    }
    if (query.empty()) {
        if (sidebarSearchTreeMode_) {
            resetSidebarTree();
        }
        return;
    }

    if (!sidebarIndexAvailable_ || sidebarIndexing_) {
        sidebarSearchTreeMode_ = true;
        sidebarTree_->DeleteAllItems();
        const auto root = sidebarTree_->AddRoot("Search unavailable");
        sidebarTree_->AppendItem(root, sidebarIndexing_
                                          ? "Indexing - results will appear when finished"
                                          : "Press F5 to build the search index");
        sidebarTree_->Expand(root);
        return;
    }

    rebuildSidebarSearchTree(query, fileSearch_.search(query, 100U));
}

void WxMainFrame::rebuildSidebarSearchTree(
    const std::string_view query,
    const std::vector<search::FileSearchHit>& hits) {
    if (sidebarTree_ == nullptr) {
        return;
    }

    sidebarSearchTreeMode_ = true;
    sidebarTree_->DeleteAllItems();
    const auto scope = sidebarSearchScope();
    if (scope.empty()) {
        sidebarTree_->AddRoot("Choose a folder first");
        return;
    }
    auto scopeLabel = pathUtf8(scope.filename());
    if (scopeLabel.empty()) {
        scopeLabel = pathUtf8(scope);
    }
    if (scopeLabel.empty()) {
        scopeLabel = "Search results";
    }

    auto resultsLabel = scopeLabel + " - results";
    if (sidebarIndexTruncated_) {
        resultsLabel += " (partial index)";
    }
    const auto root = sidebarTree_->AddRoot(
        fromUtf8(resultsLabel), -1, -1,
        new SidebarTreeItemData(scope, true, true));
    std::map<std::string, wxTreeItemId> nodes;
    nodes.emplace(std::string{}, root);
    wxTreeItemId firstResult;
    std::size_t visibleResults = 0U;

    for (const auto& hit : hits) {
        std::string relative = hit.relativePathUtf8;
        std::ranges::replace(relative, '\\', '/');
        while (relative.starts_with("./")) {
            relative.erase(0U, 2U);
        }
        if (relative.empty() || relative.front() == '/' ||
            (relative.size() >= 2U && relative[1U] == ':')) {
            const auto label = pathUtf8(hit.path.filename());
            if (!label.empty()) {
                const auto child = sidebarTree_->AppendItem(
                    root, fromUtf8(label), -1, -1,
                    new SidebarTreeItemData(hit.path, false, true));
                if (!firstResult.IsOk()) {
                    firstResult = child;
                }
                ++visibleResults;
            }
            continue;
        }

        std::vector<std::string_view> components;
        std::size_t begin = 0U;
        bool unsafe = false;
        while (begin <= relative.size()) {
            const auto separator = relative.find('/', begin);
            const auto end = separator == std::string::npos ? relative.size() : separator;
            const auto component = std::string_view(relative).substr(begin, end - begin);
            if (!component.empty() && component != ".") {
                if (component == "..") {
                    unsafe = true;
                    break;
                }
                components.push_back(component);
            }
            if (separator == std::string::npos) {
                break;
            }
            begin = separator + 1U;
        }

        if (unsafe || components.empty()) {
            const auto label = pathUtf8(hit.path.filename());
            if (!label.empty()) {
                const auto child = sidebarTree_->AppendItem(
                    root, fromUtf8(label), -1, -1,
                    new SidebarTreeItemData(hit.path, false, true));
                if (!firstResult.IsOk()) {
                    firstResult = child;
                }
                ++visibleResults;
            }
            continue;
        }

        auto currentPath = scope;
        auto parent = root;
        std::string key;
        for (std::size_t index = 0U; index < components.size(); ++index) {
            const auto component = components[index];
            if (!key.empty()) {
                key.push_back('/');
            }
            key.append(component);

            currentPath /= pathFromUtf8(component);
            if (const auto existing = nodes.find(key); existing != nodes.end()) {
                parent = existing->second;
                continue;
            }

            const bool isLast = index + 1U == components.size();
            const bool directory = !isLast || hit.kind == search::FileSearchEntryKind::Directory;
            const auto child = sidebarTree_->AppendItem(
                parent, fromUtf8(std::string(component)), -1, -1,
                new SidebarTreeItemData(isLast ? hit.path : currentPath, directory, true));
            nodes.emplace(key, child);
            parent = child;
            if (isLast && !directory) {
                if (!firstResult.IsOk()) {
                    firstResult = child;
                }
                ++visibleResults;
            }
        }
    }

    if (visibleResults == 0U) {
        sidebarTree_->AppendItem(root, wxString("No matches for ") + fromUtf8(std::string(query)));
    }
    sidebarTree_->ExpandAll();
    if (firstResult.IsOk()) {
        sidebarTree_->SelectItem(firstResult);
        sidebarTree_->EnsureVisible(firstResult);
    } else {
        sidebarTree_->SelectItem(root);
    }
}

bool WxMainFrame::openSidebarSelection() {
    if (sidebarTree_ == nullptr) {
        return false;
    }
    auto selection = sidebarTree_->GetSelection();
    if (!selection.IsOk()) {
        selection = sidebarTree_->GetRootItem();
        if (!selection.IsOk()) {
            return false;
        }
        sidebarTree_->SelectItem(selection);
    }

    auto* data = sidebarTreeData(sidebarTree_, selection);
    if (data == nullptr) {
        return false;
    }
    if (data->directory) {
        if (!sidebarTree_->IsExpanded(selection)) {
            sidebarTree_->Expand(selection);
        }
        return true;
    }
    return openPath(data->path);
}

#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
std::vector<automation::AutomationElementSnapshot> WxMainFrame::collectAutomationElements() {
    auto elements = collectNamedAutomationElements(*this);
    const auto presentation = presentation_.snapshot();
    for (const auto& pane : presentation.panes) {
        for (const auto& tab : pane.tabs) {
            static_cast<void>(automation::setElementText(
                elements, "tab." + std::to_string(tab.view.value), tab.title));
        }
    }
    return elements;
}

automation::AutomationStatusSnapshot WxMainFrame::collectAutomationStatus() const {
    const auto label = [](const wxStaticText* text) {
        return text != nullptr ? toUtf8(text->GetLabel()) : std::string{};
    };
    return automation::AutomationStatusSnapshot{
        .position = label(statusPositionText_),
        .encoding = label(statusEncodingText_),
        .lineEnding = label(statusLineEndingText_),
        .format = label(statusFormatText_),
        .mode = label(statusModeText_),
        .extra = label(statusExtraText_),
    };
}

automation::AutomationWindowSnapshot WxMainFrame::collectAutomationWindow() {
    return collectAutomationWindowSnapshot(*this);
}

std::string WxMainFrame::handleAutomationBridgeRequest(const std::string_view request) {
    const auto command = automation::parseBridgeCommand(request);
    if (!command) {
        return "ERROR\tbad_request";
    }

    auto promise = std::make_shared<std::promise<std::string>>();
    auto future = promise->get_future();
    auto frameRef = automationFrameRef_;
    CallAfter([frameRef, promise, command = *command]() mutable {
        auto* frame = frameRef ? frameRef->load(std::memory_order_acquire) : nullptr;
        if (frame == nullptr) {
            promise->set_value("ERROR\tshutting_down");
            return;
        }
        promise->set_value(frame->buildAutomationBridgeResponse(command));
    });

    if (future.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
        return "ERROR\ttimeout";
    }
    return future.get();
}

std::string WxMainFrame::buildAutomationBridgeResponse(const automation::BridgeCommand& command) {
    switch (command.kind) {
    case automation::BridgeCommandKind::Ping: {
        const auto window = collectAutomationWindow();
        return "PONG\t" + std::to_string(window.pid);
    }
    case automation::BridgeCommandKind::Tree: {
        const auto elements = collectAutomationElements();
        std::string response;
        for (const auto& element : elements) {
            if (!response.empty()) response.push_back('\n');
            response += automation::serializeElement(element);
        }
        return response;
    }
    case automation::BridgeCommandKind::Get: {
        const auto elements = collectAutomationElements();
        const auto found = std::find_if(elements.begin(), elements.end(), [&](const auto& element) {
            return element.id == command.semanticId;
        });
        return found != elements.end() ? automation::serializeElement(*found)
                                       : std::string("ERROR\tnot_found");
    }
    case automation::BridgeCommandKind::Status:
        return automation::serializeStatus(collectAutomationStatus());
    case automation::BridgeCommandKind::Window:
        return automation::serializeWindow(collectAutomationWindow());
    }
    return "ERROR\tbad_request";
}
#endif

std::optional<workspace::ViewId> WxMainFrame::activeView() const noexcept {
    const auto* pane = workspace_.pane(workspace_.activePane());
    return pane == nullptr ? std::nullopt : pane->activeView;
}

std::optional<core::DocumentId> WxMainFrame::activeDocumentId() const noexcept {
    const auto viewId = activeView();
    const auto* viewState = viewId ? workspace_.view(*viewId) : nullptr;
    return viewState == nullptr ? std::nullopt
                                : std::optional<core::DocumentId>{viewState->document};
}

core::Document* WxMainFrame::activeDocument() noexcept {
    const auto viewId = activeView();
    const auto* viewState = viewId ? workspace_.view(*viewId) : nullptr;
    return viewState == nullptr ? nullptr : documents_.get(viewState->document);
}

const core::Document* WxMainFrame::activeDocument() const noexcept {
    const auto viewId = activeView();
    const auto* viewState = viewId ? workspace_.view(*viewId) : nullptr;
    return viewState == nullptr ? nullptr : documents_.get(viewState->document);
}

bool WxMainFrame::documentStillViewed(const core::DocumentId document) const {
    bool found = false;
    workspace_.forEachView([&](const workspace::ViewState& view) {
        found = found || view.document == document;
    });
    return found;
}

bool WxMainFrame::documentViewedOutsideView(const core::DocumentId document,
                                            const workspace::ViewId excluded) const {
    bool found = false;
    workspace_.forEachView([&](const workspace::ViewState& view) {
        found = found || (view.id != excluded && view.document == document);
    });
    return found;
}

bool WxMainFrame::documentViewedOutsidePane(const core::DocumentId document,
                                            const workspace::PaneId excluded) const {
    bool found = false;
    workspace_.forEachPane([&](const workspace::PaneState& pane) {
        if (found || pane.id == excluded) return;
        for (const auto viewId : pane.views) {
            const auto* view = workspace_.view(viewId);
            if (view != nullptr && view->document == document) {
                found = true;
                return;
            }
        }
    });
    return found;
}

void WxMainFrame::persistSettings() noexcept {
    static_cast<void>(lifecycle_.saveSettings());
}

app::PersistentSaveResult WxMainFrame::persistApplicationState() noexcept {
    auto result = lifecycle_.saveAll();
    if (!result.issues.empty()) {
        lastMaintenanceError_ = result.issues.front().error;
    }
    return result;
}

bool WxMainFrame::documentHasFollowingView(const core::DocumentId document) const {
    bool found = false;
    workspace_.forEachView([&](const workspace::ViewState& view) {
        if (found || view.document != document) return;
        const auto* runtime = presentation_.viewRuntime(view.id);
        found = runtime != nullptr && runtime->openMode == core::OpenMode::Viewer &&
                runtime->followEnabled;
    });
    return found;
}

void WxMainFrame::handleMaintenance(const app::ApplicationMaintenanceResult& result) {
    for (const auto& item : result.autoSave) {
        if (item.outcome == recovery::AutoSaveOutcome::RecoveryCheckpoint ||
            item.outcome == recovery::AutoSaveOutcome::FileSaved) {
            static_cast<void>(persistTextAppearance(item.documentId));
        }
    }
    for (const auto& fileEvent : result.fileEvents) {
        const auto* document = documents_.get(fileEvent.documentId);
        if (document == nullptr) {
            continue;
        }
        const auto state = document->externalChangeState();
        const bool followOwnsChange = documentHasFollowingView(fileEvent.documentId) &&
                                     !document->textBufferLoaded() && !document->modified();
        const bool conflict = !followOwnsChange &&
                              (document->requiresExplicitOverwrite() ||
                               state == storage::FileChangeState::Modified ||
                               state == storage::FileChangeState::Deleted ||
                               state == storage::FileChangeState::Inaccessible);
        presentation_.setExternalChangeState(
            fileEvent.documentId, conflict ? state : storage::FileChangeState::Unchanged);
    }

    std::error_code error = result.sessionSaveError;
    if (!error) {
        for (const auto& item : result.autoSave) {
            if (item.outcome == recovery::AutoSaveOutcome::Failed && item.error) {
                error = item.error;
                break;
            }
        }
    }

    if (error) {
        if (error != lastMaintenanceError_) {
            lastMaintenanceError_ = error;
            showError("Recovery failed", error);
        }
    } else {
        lastMaintenanceError_.clear();
    }

    if (result.changed()) {
        shell_.requestRefresh();
        scheduleSynchronize();
    }
}

void WxMainFrame::showError(const wxString& title, const std::error_code& error) {
    const auto message = fromUtf8(error.message());
    wxMessageBox(message.empty() ? wxString{"Unknown error"} : message,
                 title,
                 wxOK | wxICON_ERROR,
                 this);
}

void WxMainFrame::updateWindowTitle(
    const app::ApplicationPresentationSnapshot& presentation) {
    const auto applyTitle = [this](const wxString& title) {
        const auto key = toUtf8(title);
        if (key == windowTitleCache_) return;
        windowTitleCache_ = key;
        SetTitle(title);
        if (titleBar_ != nullptr) titleBar_->setTitle(title);
    };

    wxString title = "notepadFasaFiso";
    if (presentation.activeView) {
        for (const auto& pane : presentation.panes) {
            for (const auto& tab : pane.tabs) {
                if (tab.view == *presentation.activeView) {
                    title = fromUtf8(tab.title);
                    if (tab.modified) {
                        title = "* " + title;
                    }
                    title += " - notepadFasaFiso";
                    applyTitle(title);
                    return;
                }
            }
        }
    }
    applyTitle(title);
}

}
