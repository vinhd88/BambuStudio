#include "AgentBridgeConnectReport.hpp"

#include <ctime>

#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>

#include <wx/clipbrd.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MsgDialog.hpp"

#include "AgentBridgeAgentDataFolder.hpp"
#include "AgentBridgeAiAppConfigsCordisYaml.hpp"

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
const char *const CONNECTIONS_LOG = "agent-connections.log";

// A list item on its own line. UTF-8 spelled out: a narrow literal is not converted as UTF-8 everywhere.
wxString bullet() { return wxString::FromUTF8("\n  \xE2\x80\xA2 "); }

// Always single-quoted: safe for any character in zsh and bash.
std::string shell_quoted(const std::string &arg)
{
    std::string quoted = "'";
    for (char c : arg)
        quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return quoted + "'";
}

// What to set up by hand when connecting fails: the config snippet, or the CLI command.
std::string manual_setup(const AiApp &app, const McpEntry &entry)
{
    if (app.cli.empty())
        return app.format == ConfigFormat::CordisYaml ? cordis_yaml_snippet(entry) : json_snippet(entry);
    // Claude Code refuses to add over an existing entry: remove it first (harmless if there is none).
    std::string command = app.id == "claude_code" ? std::string("claude mcp remove -s user ") + MCP_ENTRY_NAME + "; claude mcp add -s user " : app.cli + " mcp add ";
    command += std::string(MCP_ENTRY_NAME) + " -- " + shell_quoted(entry.command);
    for (const std::string &arg : entry.args)
        command += " " + shell_quoted(arg);
    return command;
}

// Where the manual setup goes, for the user.
wxString manual_how(const AiApp &app)
{
    const wxString file = wxString::FromUTF8(app.config);
    return !app.cli.empty()                           ? _L("Run it in Terminal.")
           : app.format == ConfigFormat::CordisYaml ? wxString::Format(_L("Add it at the end of %s; DeepSeek Harness reloads it in a few seconds."), file)
                                                    : wxString::Format(_L("Paste it into %s under mcpServers."), file);
}

void copy_to_clipboard(const wxString &text)
{
    if (wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(text));
        wxTheClipboard->Close();
    }
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
    boost::nowide::ofstream log((boost::filesystem::path(data_dir()) / CONNECTIONS_LOG).string(), std::ios::app);
    log << stamp << "  " << app.id << "  from " << trigger << "  " << (result.ok ? "connected" : "failed: " + result.message)
        << (result.backup.empty() ? "" : "  backup " + result.backup) << '\n';
}

// Harness reloads its patch file by itself; the others read their config at start.
wxString after_connect_hint(const AiApp &app)
{
    const wxString name = wxString::FromUTF8(app.name);
    return app.format == ConfigFormat::CordisYaml ? _L("It picks up the change in a few seconds.") : wxString::Format(_L("Restart %s to use it."), name);
}
} // namespace

std::string home_dir() { return wxGetHomeDir().ToUTF8().data(); }

McpEntry this_studio_entry()
{
    const std::string official = wxStandardPaths::Get().GetUserDataDir().ToUTF8().data();
    return studio_entry(wxStandardPaths::Get().GetExecutablePath().ToUTF8().data(), data_dir(), agent_data_dir_for(official));
}

std::string temporary_location(const std::string &binary)
{
    if (binary.find("/AppTranslocation/") != std::string::npos)
        return "Studio is running from a temporary place, so AI apps could not find it later. Move it to the Applications folder, open it "
               "from there, then connect.";
    return "";
}

ConnectOutcome connect_quietly(const AiApp &app, const McpEntry &entry, const char *trigger)
{
    ConnectOutcome outcome{app, {}};
    if (const std::string place = temporary_location(entry.command); !place.empty())
        outcome.result.message = place;
    else {
        wxBusyCursor busy;
        outcome.result = connect_app(app, entry, home_dir());
    }
    log_connect(app, trigger, outcome.result);
    return outcome;
}

wxString connected_text(const ConnectOutcome &outcome)
{
    wxString text = wxString::Format(_L("%s is connected to this Studio."), wxString::FromUTF8(outcome.app.name)) + " " + after_connect_hint(outcome.app);
    if (!outcome.result.backup.empty())
        text += "\n" + _L("Its previous settings were saved to:") + " " + wxString::FromUTF8(outcome.result.backup);
    return text;
}

void report_failure(wxWindow *parent, const ConnectOutcome &outcome, const McpEntry &entry)
{
    const std::string manual = manual_setup(outcome.app, entry);
    copy_to_clipboard(wxString::FromUTF8(manual));
    const ConnectResult &result = outcome.result;
    const wxString state = result.changed ? _L("Its settings were changed; the previous ones are in:") + "\n" + wxString::FromUTF8(result.backup)
                                          : _L("Nothing was changed.");
    MessageDialog(parent,
                  wxString::Format(_L("Could not connect %s: %s"), wxString::FromUTF8(outcome.app.name), wxString::FromUTF8(result.message)) + "\n\n" +
                      state + "\n\n" + _L("The setup was copied to the clipboard.") + " " + manual_how(outcome.app) + "\n\n" + wxString::FromUTF8(manual),
                  _L("Connect AI apps"), wxOK | wxICON_WARNING)
        .ShowModal();
}

void report_summary(wxWindow *parent, const std::vector<ConnectOutcome> &outcomes, const McpEntry &entry)
{
    wxString connected, failed, clipboard;
    bool     backed_up = false;
    for (const ConnectOutcome &outcome : outcomes) {
        const wxString name = wxString::FromUTF8(outcome.app.name);
        backed_up |= !outcome.result.backup.empty();
        if (outcome.result.ok) {
            connected += bullet() + name + ": " + after_connect_hint(outcome.app);
            continue;
        }
        failed += bullet() + name + ": " + wxString::FromUTF8(outcome.result.message);
        if (outcome.result.changed)
            failed += " " + _L("(its settings were changed)");
        // Each failed app's setup under a heading saying where it goes.
        clipboard += "# " + name + ": " + manual_how(outcome.app) + "\n" + wxString::FromUTF8(manual_setup(outcome.app, entry)) + "\n\n";
    }

    wxString text;
    if (!connected.empty())
        text += _L("Connected to this Studio:") + connected;
    if (!failed.empty()) {
        copy_to_clipboard(clipboard);
        text += (text.empty() ? "" : "\n\n") + _L("Could not connect:") + failed + "\n\n" +
                _L("Their setups were copied to the clipboard, each under a heading saying where it goes.");
    }
    if (backed_up)
        text += "\n\n" + _L("Previous settings were backed up first; the backup paths are listed in:") + "\n" +
                wxString::FromUTF8((boost::filesystem::path(data_dir()) / CONNECTIONS_LOG).string());
    MessageDialog(parent, text, _L("Connect AI apps"), wxOK | (failed.empty() ? wxICON_INFORMATION : wxICON_WARNING)).ShowModal();
}

}}} // namespace Slic3r::GUI::AgentBridge
