#include "WxEditorHost.hpp"

#include <wx/app.h>
#include <wx/frame.h>
#include <wx/stc/stc.h>

#include <iostream>

class FontTestApp final : public wxApp {
public:
    bool OnInit() override { return true; }
};

wxIMPLEMENT_APP_NO_MAIN(FontTestApp);

namespace {

int failures = 0;

void expect(const bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

// These tests bind existing text; editing callbacks must never be needed.
class NoEdits final : public nff::gui::IEditorHostEvents {
public:
    bool applyTextEdit(nff::workspace::ViewId, const nff::gui::EditorTextEdit&) override {
        expect(false, "font changes must not edit document text");
        return false;
    }
    bool restoreTextAppearanceRange(
        nff::workspace::ViewId, std::uint64_t, std::uint64_t,
        std::span<const nff::metadata::TextAppearanceSpan>) override {
        expect(false, "binding font changes must not request undo restoration");
        return false;
    }
};

wxStyledTextCtrl* findEditor(wxWindow& window) {
    if (auto* editor = dynamic_cast<wxStyledTextCtrl*>(&window)) {
        return editor;
    }
    for (auto* child : window.GetChildren()) {
        if (auto* editor = findEditor(*child)) {
            return editor;
        }
    }
    return nullptr;
}

void testRangeFontRendering(wxFrame& frame) {
    nff::settings::AppSettings settings;
    auto theme = nff::gui::wxbackend::darkTheme(settings.accent);
    NoEdits events;
    nff::gui::wxbackend::WxEditorHost host(
        frame, {1U}, events, theme, settings, {}, {}, {}, {}, {});
    auto* editor = findEditor(frame);
    expect(editor != nullptr, "real wx editor is created");
    if (editor == nullptr) return;

    nff::metadata::TextAppearanceMap appearance;
    const auto serif = appearance.internFontFamily("Times New Roman");
    expect(serif.has_value(), "first family is interned");
    if (!serif) return;
    expect(appearance.setFontFamily(0U, 4U, *serif), "first range accepts a font");
    expect(appearance.setFontSize(0U, 4U, 18U), "first range accepts independent size");
    appearance.setForeground(0U, 4U, 0xFF123456U);

    nff::gui::EditorHostBinding binding;
    binding.document = {1U};
    binding.text = "iiii WWWW plain";
    binding.documentRevision = 1U;
    const auto bind = [&] {
        binding.appearanceSpans = appearance.spans();
        binding.appearanceFontFamilies = appearance.fontFamilies();
        ++binding.appearanceRevision;
        host.bind(binding);
    };
    const auto faceAt = [&](const int position) {
        return editor->StyleGetFaceName(editor->GetStyleAt(position));
    };
    bind();
    expect(faceAt(0) == "Times New Roman", "single/first font family reaches Scintilla");

    const auto mono = appearance.internFontFamily("Courier New");
    expect(mono.has_value(), "second family is interned");
    if (!mono) return;
    expect(appearance.setFontFamily(5U, 9U, *mono), "second range accepts another font");
    bind();
    expect(faceAt(0) == "Times New Roman", "first range keeps its own font in mixed text");
    expect(faceAt(5) == "Courier New", "last family in the table reaches Scintilla");
    expect(editor->StyleGetSize(editor->GetStyleAt(0)) == 18,
           "range size remains independent of font family");
    expect(editor->StyleGetForeground(editor->GetStyleAt(0)) == wxColour(0x12, 0x34, 0x56),
           "range color remains independent of font family");

    editor->SetSelection(1, 8);
    binding.fontFamily = "Arial";
    bind();
    expect(faceAt(10) == "Arial", "unformatted text follows the view font");
    expect(faceAt(0) == "Times New Roman" && faceAt(5) == "Courier New",
           "view font changes preserve explicit mixed fonts");
    expect(editor->GetAnchor() == 1 && editor->GetCurrentPos() == 8,
           "font changes preserve selection");
    binding.fontFamily = {};
    bind();
    const auto defaultFace = editor->StyleGetFaceName(wxSTC_STYLE_DEFAULT);
    appearance.clearFontFamily(0U, 4U);
    bind();
    expect(faceAt(0) == defaultFace,
           "cleared range font inherits system default despite reused style slot");
    expect(editor->StyleGetSize(editor->GetStyleAt(0)) == 18 &&
           editor->StyleGetForeground(editor->GetStyleAt(0)) == wxColour(0x12, 0x34, 0x56),
           "clearing family preserves range size and color");
    expect(faceAt(5) == "Courier New", "clearing one range preserves another range font");
    host.refreshAppearance();
    expect(faceAt(0) == defaultFace && faceAt(5) == "Courier New",
           "appearance refresh preserves inherited and explicit fonts");
    expect(editor->GetText() == "iiii WWWW plain" && !editor->CanUndo(),
           "format binding preserves document bytes and edit history");
}

}

int main(int argc, char** argv) {
    if (!wxEntryStart(argc, argv)) return 2;
    if (!wxTheApp->CallOnInit()) {
        wxEntryCleanup();
        return 2;
    }
    {
        wxFrame frame(nullptr, wxID_ANY, "Font rendering regression tests");
        testRangeFontRendering(frame);
    }
    wxTheApp->OnExit();
    wxEntryCleanup();
    std::cout << "wx font rendering: " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
