#include "AgentBridgeConnectionsSection.hpp"

#include <algorithm>
#include <ctime>

#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>

#include <wx/busyinfo.h>
#include <wx/clipbrd.h>
#include <wx/sizer.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"

#include "AgentBridge.hpp"
#include "AgentBridgeAgentDataFolder.hpp"
#include "AgentBridgeAiAppConnect.hpp"

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
// App config key: the Studio copy and data folder for which the user declined the startup offer.
const char *const DECLINED_KEY = "agent_connection_check_declined";

std::string home_dir() { return wxGetHomeDir().ToUTF8().data(); }

// The entry that reaches this Studio: this binary, plus --datadir unless it runs on its default folder.
McpEntry this_studio_entry()
{
    const std::string official = wxStandardPaths::Get().GetUserDataDir().ToUTF8().data();
    return studio_entry(wxStandardPaths::Get().GetExecutablePath().ToUTF8().data(), data_dir(), agent_data_dir_for(official));
}

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

// Always single-quoted: safe for any character in zsh and bash.
std::string shell_quoted(const std::string &arg)
{
    std::string quoted = "'";
    for (char c : arg)
        quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return quoted + "'";
}

// What to set up by hand when connecting fails: the JSON snippet, or the CLI command.
std::string manual_setup(const AiApp &app, const McpEntry &entry)
{
    if (app.cli.empty())
        return json_snippet(entry);
    // Claude Code refuses to add over an existing entry: remove it first (harmless if there is none).
    std::string command = app.id == "claude_code" ? std::string("claude mcp remove -s user ") + MCP_ENTRY_NAME + "; claude mcp add -s user " : app.cli + " mcp add ";
    command += std::string(MCP_ENTRY_NAME) + " -- " + shell_quoted(entry.command);
    for (const std::string &arg : entry.args)
        command += " " + shell_quoted(arg);
    return command;
}

// Why an entry for this Studio would stop working soon, or empty: macOS runs a downloaded app from a
// temporary copy until it is moved.
std::string temporary_location(const std::string &binary)
{
    if (binary.find("/AppTranslocation/") != std::string::npos)
        return "Studio is running from a temporary place, so AI apps could not find it later. Move it to the Applications folder, open it "
               "from there, then connect.";
    return "";
}

// One line per connect in <data folder>/agent-connections.log: what changed the AI apps' configs,
// when, and from where. Studio's own logs are encrypted.
void log_connect(const AiApp &app, const char *trigger, const ConnectResult &result)
{
    const std::time_t now = std::time(nullptr);
    char              stamp[32];
    std::tm           local{};
    localtime_r(&now, &local);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
    boost::nowide::ofstream log((boost::filesystem::path(data_dir()) / "agent-connections.log").string(), std::ios::app);
    log << stamp << "  " << app.id << "  from " << trigger << "  " << (result.ok ? "connected" : "failed: " + result.message)
        << (result.backup.empty() ? "" : "  backup " + result.backup) << '\n';
}

// Connects one app and tells the user how it went. Returns whether it worked.
bool connect_and_report(wxWindow *parent, const AiApp &app, const McpEntry &entry, const char *trigger)
{
    ConnectResult result;
    if (const std::string place = temporary_location(entry.command); !place.empty())
        result.message = place;
    else {
        wxBusyCursor busy;
        result = connect_app(app, entry, home_dir());
    }
    log_connect(app, trigger, result);
    const wxString name = wxString::FromUTF8(app.name);
    if (result.ok) {
        wxString text = wxString::Format(_L("%s is connected to this Studio. Restart %s to use it."), name, name);
        if (!result.backup.empty())
            text += "\n\n" + _L("Its previous settings were saved to:") + "\n" + wxString::FromUTF8(result.backup);
        MessageDialog(parent, text, _L("Connect AI apps"), wxOK | wxICON_INFORMATION).ShowModal();
        return true;
    }
    const std::string manual = manual_setup(app, entry);
    if (wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(manual)));
        wxTheClipboard->Close();
    }
    const wxString how = app.cli.empty() ? wxString::Format(_L("Paste it into %s under mcpServers."), wxString::FromUTF8(app.config))
                                         : _L("Run it in Terminal.");
    wxString state = result.changed ? _L("Its settings were changed; the previous ones are in:") + "\n" + wxString::FromUTF8(result.backup)
                                    : _L("Nothing was changed.");
    MessageDialog(parent,
                  wxString::Format(_L("Could not connect %s: %s"), name, wxString::FromUTF8(result.message)) + "\n\n" + state + "\n\n" +
                      _L("The setup was copied to the clipboard.") + " " + how + "\n\n" + wxString::FromUTF8(manual),
                  _L("Connect AI apps"), wxOK | wxICON_WARNING)
        .ShowModal();
    return false;
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
        rows[i].second->Bind(wxEVT_BUTTON, [page, app = apps[i], refresh](wxCommandEvent &) {
            connect_and_report(page, app, this_studio_entry(), "preferences button");
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
        for (const AiApp &app : elsewhere)
            connect_and_report(wxGetApp().mainframe, app, entry, "startup offer");
    });
}

}}} // namespace Slic3r::GUI::AgentBridge
