#ifndef slic3r_AgentBridgeConnectReport_hpp_
#define slic3r_AgentBridgeConnectReport_hpp_

#include <string>
#include <vector>

#include <wx/string.h>

#include "AgentBridgeAiAppConnect.hpp"

class wxWindow;

// Connecting AI apps to this Studio and telling the user how it went: the startup offer shows one
// dialog however many apps it connected; a Preferences button shows a dialog only when it fails
// (its row shows a success). UI thread.
namespace Slic3r { namespace GUI { namespace AgentBridge {

struct ConnectOutcome
{
    AiApp         app;
    ConnectResult result;
};

std::string home_dir();

// The entry that reaches this Studio: this binary, plus --datadir unless it runs on its default folder.
McpEntry this_studio_entry();

// Why an entry for this Studio would stop working soon, or empty: macOS runs a downloaded app from a
// temporary copy until it is moved.
std::string temporary_location(const std::string &binary);

// Connects one app and logs it to <data folder>/agent-connections.log. Shows nothing. `trigger` says
// where the connect came from, for the log.
ConnectOutcome connect_quietly(const AiApp &app, const McpEntry &entry, const char *trigger);

// "Claude Code is connected to this Studio. Restart Claude Code to use it.", plus the backup path.
wxString connected_text(const ConnectOutcome &outcome);

// One app failed: a warning dialog with the manual setup, which is also put on the clipboard.
void report_failure(wxWindow *parent, const ConnectOutcome &outcome, const McpEntry &entry);

// Several apps connected at once: a single dialog listing each one. Manual setups of the failed
// ones go to the clipboard together.
void report_summary(wxWindow *parent, const std::vector<ConnectOutcome> &outcomes, const McpEntry &entry);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
