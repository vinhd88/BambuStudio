#ifndef slic3r_AgentBridgeAiAppConfigs_hpp_
#define slic3r_AgentBridgeAiAppConfigs_hpp_

#include <optional>
#include <string>
#include <vector>

// The `bambu-studio` MCP entry in AI app configs: build it, read it from JSON and TOML config text,
// compare it, and write it into JSON config text. Text only, no files. GUI-free.
namespace Slic3r { namespace GUI { namespace AgentBridge {

constexpr const char *MCP_ENTRY_NAME = "bambu-studio";

struct McpEntry
{
    std::string              command;
    std::vector<std::string> args;
    bool operator==(const McpEntry &other) const { return command == other.command && args == other.args; }
};

enum class ConnectionState
{
    NotInstalled, // the app is not on this Mac
    NotConnected, // no bambu-studio entry
    Connected,    // the entry runs this Studio's helper on this Studio's data folder
    Different,    // an entry for another copy of Studio, another data folder, or of another shape
    Unreadable,   // the config file cannot be read or parsed
};

// The entry that reaches this Studio: its helper, plus --datadir unless Studio uses its default folder.
McpEntry studio_entry(const std::string &binary, const std::string &data_dir, const std::string &default_data_dir);

// The entry under mcpServers in a JSON config (Claude Desktop, Claude Code, Antigravity). Empty if
// there is none. Throws if the text is not a JSON object.
std::optional<McpEntry> read_json_entry(const std::string &text);

// The entry in Codex's config.toml ([mcp_servers.bambu-studio] with command and args). Empty if there
// is none; an entry it cannot read comes back with an empty command.
std::optional<McpEntry> read_toml_entry(const std::string &text);

ConnectionState compare_entry(const std::optional<McpEntry> &found, const McpEntry &expected);

// `text` with only mcpServers.bambu-studio set to `entry`; everything else as it was. Empty or
// whitespace-only text starts a new config. Throws if the text is not a JSON object.
std::string with_json_entry(const std::string &text, const McpEntry &entry);

// The entry as an mcpServers snippet to paste by hand.
std::string json_snippet(const McpEntry &entry);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
