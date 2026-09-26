#include "WxMainFrame.hpp"
#include "WxStartupSplash.hpp"

#include "notepadFasaFiso/app/LaunchRequest.hpp"
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
#include "notepadFasaFiso/gui/automation/AutomationCommandLine.hpp"
#endif
#include "notepadFasaFiso/diagnostics/LocalDiagnostics.hpp"
#include "notepadFasaFiso/platform/Platform.hpp"

#include <wx/app.h>
#include <wx/image.h>
#include <wx/imagpng.h>
#include <wx/msgdlg.h>
#include <wx/version.h>

#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nff::gui::wxbackend {

class WxApplication final : public wxApp {
public:
    bool OnInit() override {

        const auto initialized = diagnostics::initialize(platform::applicationDataDirectory());
        if (initialized) {
            diagnostics::installPlatformCrashHandler(initialized.paths);
        }
#if wxCHECK_VERSION(3, 3, 0)
        static_cast<void>(SetAppearance(wxAppBase::Appearance::Dark));
#endif
        wxImage::AddHandler(new wxPNGHandler);
        SetAppName("notepadFasaFiso");
        SetAppDisplayName("notepadFasaFiso");

        std::vector<std::string> argumentStorage;
        argumentStorage.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
        for (int index = 1; index < argc; ++index) {
            const wxString argument = argv[index];
            const auto buffer = argument.utf8_str();
            if (buffer.data() == nullptr) {
                wxMessageBox("A command-line argument could not be converted to UTF-8.",
                             "Command line error", wxOK | wxICON_ERROR);
                return false;
            }
            argumentStorage.emplace_back(buffer.data(), buffer.length());
        }

#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
        const auto automationStateRoot =
            automation::extractAutomationStateRootArgument(argumentStorage);
        if (!automationStateRoot) {
            wxMessageBox(wxString::FromUTF8(automationStateRoot.error().data(), automationStateRoot.error().size()),
                         "Automation launch error", wxOK | wxICON_ERROR);
            return false;
        }
        if (automationStateRoot->has_value()) {
            const auto rootText = wxString::FromUTF8(automationStateRoot->value().data(), automationStateRoot->value().size());
            if (rootText.empty()) {
                wxMessageBox("Automation state root could not be converted to a Windows path.",
                             "Automation launch error", wxOK | wxICON_ERROR);
                return false;
            }
            automationStateRoot_ = std::filesystem::path(rootText.ToStdWstring());
        }
#endif

        std::vector<std::string_view> argumentViews;
        argumentViews.reserve(argumentStorage.size());
        for (const auto& argument : argumentStorage) {
            argumentViews.emplace_back(argument);
        }
        auto parsed = app::LaunchRequestParser::parse(argumentViews);
        if (!parsed) {
            wxMessageBox(wxString::FromUTF8(parsed.error.c_str(), parsed.error.size()),
                         "Command line error", wxOK | wxICON_ERROR);
            return false;
        }
        if (parsed.request.showHelp) {
            const auto usage = app::LaunchRequestParser::usage();
            wxMessageBox(wxString::FromUTF8(usage.data(), usage.size()),
                         "notepadFasaFiso command line", wxOK | wxICON_INFORMATION);
            return false;
        }

        pendingRequest_ = std::move(parsed.request);
        splash_ = new WxStartupSplash([this]() { finishStartup(); });
        SetTopWindow(splash_);
        splash_->Show(true);
        splash_->Raise();
        return true;
    }

private:
    void finishStartup() {
        if (!pendingRequest_ || splash_ == nullptr) {
            return;
        }

        auto request = std::move(*pendingRequest_);
        pendingRequest_.reset();
        auto* splash = splash_;

        WxMainFrame* frame = nullptr;
        try {
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
            frame = new WxMainFrame(std::move(request), std::move(automationStateRoot_));
#else
            frame = new WxMainFrame(std::move(request));
#endif
        } catch (...) {
            splash_ = nullptr;
            splash->Destroy();
            throw;
        }

        frame->Show(true);
        SetTopWindow(frame);
        splash_ = nullptr;
        splash->Hide();
        splash->Destroy();
    }

    std::optional<app::LaunchRequest> pendingRequest_;
#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION
    std::optional<std::filesystem::path> automationStateRoot_;
#endif
    WxStartupSplash* splash_{nullptr};

public:
    bool OnExceptionInMainLoop() override {
        try {
            throw;
        } catch (const std::exception& exception) {
            diagnostics::writeExceptionReport("wx.exception", exception.what());
        } catch (...) {
            diagnostics::writeExceptionReport("wx.exception",
                                              "unknown C++ exception in main loop");
        }
        return false;
    }

    void OnUnhandledException() override {
        diagnostics::writeExceptionReport(
            "wx.unhandled",
            "unhandled C++ exception; application terminating");
    }

    int OnExit() override {
        diagnostics::shutdown();
        return wxApp::OnExit();
    }
};

}

wxIMPLEMENT_APP(nff::gui::wxbackend::WxApplication);
