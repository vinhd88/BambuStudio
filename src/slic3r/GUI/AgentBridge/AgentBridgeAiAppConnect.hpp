#ifndef slic3r_AgentBridgeAiAppConnect_hpp_
#define slic3r_AgentBridgeAiAppConnect_hpp_

#include <string>
#include <vector>

#include "AgentBridgeAiAppConfigs.hpp"

// Registers this Studio's MCP helper in the AI apps on this Mac, and reports whether each one is
// registered. Touches only the bambu-studio entry, after a backup of the app's config. GUI-free; the
// connect call runs the app's own CLI and can take a few seconds.
namespace Slic3r { namespace GUI { namespace AgentBridge {

struct AiApp
{
    std::string id;     // claude_desktop, claude_code, codex, antigravity
    std::string name;   // shown to the user
    std::string config; // the file holding its MCP servers
    std::string cli;    // command-line tool that edits it ("claude", "codex", "agy"); empty: Studio edits the JSON file
};

// The supported apps, with config paths under `home`.
std::vector<AiApp> ai_apps(const std::string &home);

struct AppStatus
{
    ConnectionState state = ConnectionState::NotInstalled;
    std::string     found_command; // for Different: the program the entry runs now
    std::string     cli_path;      // where its CLI is; empty for Claude Desktop or when missing
};

// Reads the app's config; never runs anything except, for a CLI not in the usual folders, the login
// shell to look it up. Never throws.
AppStatus app_status(const AiApp &app, const McpEntry &expected, const std::string &home);

struct ConnectResult
{
    bool        ok = false;
    std::string message; // why it failed, for the user; empty on success
    bool        changed = false; // the app's config was changed (on failure: left changed, see the backup)
    std::string backup;  // the config backup made first, if the config existed
};

// Sets the app's bambu-studio entry to `entry`, then reads it back to check. Never throws.
ConnectResult connect_app(const AiApp &app, const McpEntry &entry, const std::string &home);

// The CLI's full path: the usual install folders first, then the login shell. Empty if not found.
std::string find_cli(const std::string &name, const std::string &home);

// Apps whose bambu-studio entry runs another copy of Studio or uses another data folder. Reads the
// config files only; for the startup check. Never throws.
std::vector<AiApp> apps_pointing_elsewhere(const McpEntry &expected, const std::string &home);

// True while Claude Desktop runs: it rewrites its config file and could drop the new entry.
bool claude_desktop_running();

}}} // namespace Slic3r::GUI::AgentBridge

#endif
