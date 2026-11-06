#include "AgentBridgePreferencesTab.hpp"

#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/CheckBox.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"

#include "AgentBridge.hpp"

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
// Matches the other Preferences pages: titles and rows indented the same way.
constexpr int TITLE_PADDING = 48;
constexpr int ROW_PADDING   = 48 + 16;
constexpr int TEXT_WIDTH    = 460;

wxString status_text(const BridgeStatus &status)
{
    if (!status.enabled)
        return _L("Off. AI apps cannot reach Studio.");
    if (status.running)
        return wxString::Format(_L("On. Listening on 127.0.0.1:%d, for this computer only."), int(status.port));
    // System error text is not always valid UTF-8; never show an empty reason.
    wxString reason = wxString::FromUTF8(status.problem);
    if (reason.empty())
        reason = _L("Studio could not start agent access.");
    return _L("On, but not running.") + " " + reason;
}

// Scrolled page, as every Preferences tab is; it must not jump to a child that takes focus.
class Page : public wxScrolledWindow
{
public:
    explicit Page(wxWindow *parent) : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL)
    {
        SetScrollRate(5, 5);
        SetBackgroundColour(*wxWHITE);
    }
    bool ShouldScrollToChildOnFocus(wxWindow *) override { return false; }
};

::Label *text(wxWindow *parent, const wxString &value, const wxFont &font, const wxColour &colour)
{
    auto *label = new ::Label(parent, font, value);
    label->SetForegroundColour(colour);
    label->Wrap(parent->FromDIP(TEXT_WIDTH));
    return label;
}

Button *plain_button(wxWindow *parent, const wxString &label)
{
    // Styled as the Browse button on the Other page, including a visibly disabled state.
    auto      *button = new Button(parent, label);
    StateColor bg(std::pair<wxColour, int>(ThemeColor::White, StateColor::Disabled), std::pair<wxColour, int>(ThemeColor::BrandGreenPressed, StateColor::Pressed),
                  std::pair<wxColour, int>(ThemeColor::BrandGreenHovered, StateColor::Hovered), std::pair<wxColour, int>(ThemeColor::White, StateColor::Enabled),
                  std::pair<wxColour, int>(ThemeColor::White, StateColor::Normal));
    button->SetBackgroundColor(bg);
    StateColor fg(std::pair<wxColour, int>(ThemeColor::TextDisabled, StateColor::Disabled), std::pair<wxColour, int>(ThemeColor::TextPrimary, StateColor::Enabled));
    button->SetBorderColor(fg);
    button->SetTextColor(fg);
    button->SetFont(::Label::Body_12);
    button->SetCornerRadius(parent->FromDIP(4));
    return button;
}
} // namespace

wxWindow *create_preferences_tab(wxWindow *parent)
{
    auto *page  = new Page(parent);
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    const auto row_flags = wxSizerFlags().Border(wxLEFT, page->FromDIP(ROW_PADDING));

    sizer->AddSpacer(page->FromDIP(24));
    sizer->Add(text(page, _L("AI agent access"), ::Label::Head_13, ThemeColor::TextSecondary), wxSizerFlags().Border(wxLEFT, page->FromDIP(TITLE_PADDING)));
    sizer->AddSpacer(page->FromDIP(12));

    // Switch row: explanation on the left, check box on the right.
    auto *switch_row = new wxBoxSizer(wxHORIZONTAL);
    switch_row->Add(text(page, _L("Let AI apps (Claude, Codex, Gemini) read and change the project open in Studio, and slice it. They cannot print or control printers."),
                         ::Label::Body_14, ThemeColor::TextPrimary),
                    wxSizerFlags().CenterVertical().Border(wxRIGHT, page->FromDIP(16)));
    auto *enabled = new ::CheckBox(page);
    switch_row->Add(enabled, wxSizerFlags().CenterVertical());
    sizer->Add(switch_row, row_flags);
    sizer->AddSpacer(page->FromDIP(10));

    auto *status = text(page, "", ::Label::Body_12, ThemeColor::TextSecondary);
    sizer->Add(status, row_flags);
    sizer->AddSpacer(page->FromDIP(20));

    auto *reset = plain_button(page, _L("Reset access token"));
    sizer->Add(reset, row_flags);
    sizer->AddSpacer(page->FromDIP(6));
    sizer->Add(text(page, _L("Gives Studio a new access token. AI apps set up for this build keep working; anything that copied the old token is locked out."),
                    ::Label::Body_12, ThemeColor::TextSecondary),
               row_flags);

    // Read the state each time the page shows: the dialog can outlive changes made elsewhere.
    const auto refresh = [page, enabled, status, reset] {
        const BridgeStatus now = bridge_status();
        enabled->SetValue(now.enabled);
        status->SetLabel(status_text(now));
        status->Wrap(page->FromDIP(TEXT_WIDTH));
        reset->Enable(now.running);
        page->Layout();
        page->FitInside();
    };
    enabled->Bind(wxEVT_TOGGLEBUTTON, [enabled, refresh](wxCommandEvent &event) {
        set_bridge_enabled(enabled->GetValue());
        refresh();
        // The check box's own handler redraws the tick; it runs only if this one passes the event on.
        event.Skip();
    });
    reset->Bind(wxEVT_BUTTON, [refresh](wxCommandEvent &) {
        reset_bridge_token();
        refresh();
    });
    page->Bind(wxEVT_SHOW, [refresh](wxShowEvent &event) {
        if (event.IsShown())
            refresh();
        event.Skip();
    });

    page->SetSizer(sizer);
    refresh();
    return page;
}

}}} // namespace Slic3r::GUI::AgentBridge
