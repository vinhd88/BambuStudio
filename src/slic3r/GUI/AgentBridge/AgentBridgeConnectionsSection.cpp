#include "AgentBridgeConnectionsSection.hpp"

#include <algorithm>

#include <wx/busyinfo.h>
#include <wx/sizer.h>

#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"

#include "AgentBridge.hpp"
#include "AgentBridgeAiAppConnect.hpp"
#include "AgentBridgeConnectReport.hpp"

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
// App config key: the Studio copy and data folder for which the user declined the startup offer.
const char *const DECLINED_KEY = "agent_connection_check_declined";

wxString status_text(const AppStatus &status)
{
    switch (status.state) {
    case ConnectionState::NotInstalled: return _L("Not found on this Mac");
    case ConnectionState::NotConnected: return _L("Not connected");
    case ConnectionState::Connected: return _L("Connected to this Studio");
    case ConnectionState::Different: return _L("Set up for another copy of Studio or data folder");
    case ConnectionState::Unreadable: return _L("Its settings file cannot be read");
    }
    return "";
}

} // namespace

std::function<void()> add_connections_section(wxWindow *page, wxSizer *sizer, int row_padding, int text_width,
                                              const std::function<Button *(const wxString &)> &make_button)
{
    const auto row_flags = wxSizerFlags().Border(wxLEFT, page->FromDIP(row_padding));
    auto      *intro     = new ::Label(page, ::Label::Body_12,
                                       _L("Adds this Studio to an AI app's MCP servers, so the app can use it. Only the bambu-studio entry changes, "
                                          "after a backup of the app's settings."));
    intro->SetForegroundColour(ThemeColor::TextSecondary);
    intro->Wrap(page->FromDIP(text_width));
    sizer->Add(intro, row_flags);
    sizer->AddSpacer(page->FromDIP(10));

    auto *grid = new wxFlexGridSizer(3, page->FromDIP(8), page->FromDIP(16));
    std::vector<std::pair<::Label *, Button *>> rows;
    for (const AiApp &app : ai_apps(home_dir())) {
        auto *name   = new ::Label(page, ::Label::Body_14, wxString::FromUTF8(app.name));
        auto *status = new ::Label(page, ::Label::Body_12, "");
        status->SetForegroundColour(ThemeColor::TextSecondary);
        auto *button = make_button(_L("Reconnect")); // the longer label sizes the column
        grid->Add(name, wxSizerFlags().CenterVertical());
        grid->Add(status, wxSizerFlags().CenterVertical());
        grid->Add(button, wxSizerFlags().CenterVertical());
        rows.emplace_back(status, button);
    }
    sizer->Add(grid, row_flags);
    // A successful connect is reported here, not in a dialog to close; the next click replaces it.
    auto *note = new ::Label(page, ::Label::Body_12, "");
    note->SetForegroundColour(ThemeColor::TextSecondary);
    sizer->AddSpacer(page->FromDIP(6));
    sizer->Add(note, row_flags);

    auto refresh = [page, rows] {
        const McpEntry entry = this_studio_entry();
        const auto     apps  = ai_apps(home_dir());
        for (size_t i = 0; i < apps.size(); ++i) {
            const AppStatus status = app_status(apps[i], entry, home_dir());
            rows[i].first->SetLabel(status_text(status));
            rows[i].second->Enable(status.state != ConnectionState::NotInstalled);
            rows[i].second->SetLabel(status.state == ConnectionState::Connected ? _L("Reconnect") : _L("Connect"));
        }
        page->Layout();
    };
    const auto apps = ai_apps(home_dir());
    for (size_t i = 0; i < apps.size(); ++i)
        rows[i].second->Bind(wxEVT_BUTTON, [page, note, text_width, app = apps[i], refresh](wxCommandEvent &) {
            const McpEntry       entry   = this_studio_entry();
            const ConnectOutcome outcome = connect_quietly(app, entry, "preferences button");
            note->SetLabel(outcome.result.ok ? connected_text(outcome) : wxString());
            note->Wrap(page->FromDIP(text_width));
            if (!outcome.result.ok)
                report_failure(page, outcome, entry);
            refresh();
        });
    return refresh;
}

void check_ai_app_connections()
{
    if (!bridge_enabled())
        return;
    const McpEntry    entry     = this_studio_entry();
    const std::string signature = entry.command + "|" + data_dir();
    if (!temporary_location(entry.command).empty() || wxGetApp().app_config->get(DECLINED_KEY) == signature)
        return;
    std::vector<AiApp> elsewhere = apps_pointing_elsewhere(entry, home_dir());
    // Only apps Studio can connect: their CLI must be here.
    elsewhere.erase(std::remove_if(elsewhere.begin(), elsewhere.end(),
                                   [](const AiApp &app) { return !app.cli.empty() && find_cli(app.cli, home_dir()).empty(); }),
                    elsewhere.end());
    if (elsewhere.empty())
        return;
    wxGetApp().CallAfter([entry, signature, elsewhere] {
        wxString names;
        for (const AiApp &app : elsewhere)
            names += (names.empty() ? "" : ", ") + wxString::FromUTF8(app.name);
        MessageDialog ask(wxGetApp().mainframe,
                          wxString::Format(_L("%s is set up for another copy of Studio or another data folder, so it cannot reach this one.\n\n"
                                              "Connect it to this Studio? Its settings are backed up first."),
                                           names),
                          _L("Connect AI apps"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION); // Return must not change configs
        if (ask.ShowModal() != wxID_YES) {
            wxGetApp().app_config->set(DECLINED_KEY, signature);
            // Saved now: the answer must hold even if Studio does not quit normally.
            wxGetApp().app_config->save();
            return;
        }
        // All connected first, then one dialog for all of them.
        std::vector<ConnectOutcome> outcomes;
        {
            // Each app's CLI can take a few seconds.
            wxBusyInfo working(_L("Connecting AI apps to this Studio..."), wxGetApp().mainframe);
            for (const AiApp &app : elsewhere)
                outcomes.push_back(connect_quietly(app, entry, "startup offer"));
        }
        if (outcomes.size() == 1 && !outcomes.front().result.ok)
            report_failure(wxGetApp().mainframe, outcomes.front(), entry);
        else
            report_summary(wxGetApp().mainframe, outcomes, entry);
    });
}

}}} // namespace Slic3r::GUI::AgentBridge
