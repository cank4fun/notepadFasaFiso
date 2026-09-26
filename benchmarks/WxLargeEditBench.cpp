#include "BenchmarkSupport.hpp"
#include "notepadFasaFiso/core/DocumentManager.hpp"
#include <wx/app.h>
#include <wx/frame.h>
#include <wx/stc/stc.h>
#include <charconv>
#include <chrono>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

class BenchmarkApp final : public wxApp {
public:
    bool OnInit() override { return true; }
};
wxIMPLEMENT_APP_NO_MAIN(BenchmarkApp);

namespace {
void require(bool value) { if (!value) throw std::runtime_error("benchmark operation failed"); }
template<class F> void timed(const char* label, F&& f) {
    const auto start = std::chrono::steady_clock::now();
    f();
    std::cout << label << "_ms=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() << std::endl;
}
void memory(const char* stage) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX info{};
    require(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&info), static_cast<DWORD>(sizeof(info))) != 0);
    std::cout << stage << " rss_bytes=" << info.WorkingSetSize << " peak_rss_bytes=" << info.PeakWorkingSetSize << " private_bytes=" << info.PrivateUsage << std::endl;
#else
    rusage info{};
    require(getrusage(RUSAGE_SELF, &info) == 0);
    std::cout << stage << " peak_rss_bytes=" << static_cast<std::uint64_t>(info.ru_maxrss) * 1024U << std::endl;
#endif
}
void run(std::size_t mib) {
    nff::bench::TempFixture fixture("nff-wx-large");
    const auto path = fixture.path("large.txt");
    const auto size = mib * 1024U * 1024U;
    require(!nff::bench::writeRepeatedText(path, std::string(79, 'x') + '\n', size));
    nff::core::DocumentManager manager;
    const auto opened = manager.open(path);
    require(static_cast<bool>(opened));
    timed("core_materialize", [&] { require(!manager.materializeForEdit(opened.id, size)); });
    memory("core_loaded");
    auto* document = manager.get(opened.id);
    auto frame = std::make_unique<wxFrame>(nullptr, wxID_ANY, "large editor benchmark", wxDefaultPosition, wxSize(900, 600));
    auto* control = new wxStyledTextCtrl(frame.get());
    control->SetCodePage(wxSTC_CP_UTF8);
    control->SetWrapMode(wxSTC_WRAP_NONE);
    frame->Bind(wxEVT_CLOSE_WINDOW, [](wxCloseEvent& event) {
        if (event.CanVeto()) event.Veto();
    });
    frame->Show();
    wxTheApp->Yield();
    {
        wxString text;
        timed("wxString_conversion", [&] { text = wxString::FromUTF8(document->text().data(), document->text().size()); });
        memory("wxString_loaded");
        timed("scintilla_set_text", [&] { control->SetText(text); control->EmptyUndoBuffer(); control->SetSavePoint(); });
        memory("scintilla_and_wxString");
    }
    timed("first_layout", [&] { wxTheApp->Yield(); });
    memory("visible");
    require(control->GetLength() >= 0 && static_cast<std::size_t>(control->GetLength()) == document->text().size());
    const auto position = control->GetLength() / 2;
    timed("scintilla_newline_insert", [&] { control->InsertText(position, "\n"); });
    timed("core_newline_insert", [&] { require(!document->applyEdit(static_cast<std::size_t>(position), 0, "\n")); });
    timed("scintilla_newline_remove", [&] { control->DeleteRange(position, 1); });
    timed("core_newline_remove", [&] { require(!document->applyEdit(static_cast<std::size_t>(position), 1, "")); });
    memory("edited");
}
}
int main(int argc, char** argv) {
    std::size_t mib{};
    if (argc != 2) { std::cerr << "usage: nff_wx_large_bench 64|256|512|1024\n"; return 2; }
    const std::string_view value(argv[1]);
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), mib);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        (mib != 64 && mib != 256 && mib != 512 && mib != 1024)) return 2;
    int wxArgc = 1;
    if (!wxEntryStart(wxArgc, argv)) return 2;
    int result = 0;
    try { require(wxTheApp->CallOnInit()); run(mib); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
    wxTheApp->OnExit();
    wxEntryCleanup();
    return result;
}
