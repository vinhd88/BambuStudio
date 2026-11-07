#ifndef slic3r_AgentBridgeAiAppConfigsCordisYaml_hpp_
#define slic3r_AgentBridgeAiAppConfigsCordisYaml_hpp_

#include <optional>
#include <string>

#include "AgentBridgeAiAppConfigs.hpp"

// The bambu-studio MCP row in DeepSeek Harness's profile patch file
// (~/.dsh/profiles/desktop/cordis.patch.yml): a top-level YAML list of patch rows, also rewritten by
// Harness's own Settings. A new row must sit in an `- insert:` list; a top-level row with the same id
// replaces its config. Studio owns the rows with id mcp-bambu-studio (and an insert holding only that
// row); every other line stays byte for byte. Line-based, no YAML library. Text only, GUI-free.
namespace Slic3r { namespace GUI { namespace AgentBridge {

constexpr const char *CORDIS_ROW_ID = "mcp-bambu-studio";

// The entry of the last mcp-bambu-studio row (a later row wins). Empty if there is none; a row it
// cannot read comes back with an empty command. Throws if the text is not a top-level block list.
std::optional<McpEntry> read_cordis_yaml_entry(const std::string &text);

// `text` with every mcp-bambu-studio row replaced by one insert row for `entry`, added at the end (or
// in place of a `[]` placeholder). Throws, so nothing gets written, if the text is not a top-level
// block list or an mcp-bambu-studio row is in a form it cannot remove cleanly.
std::string with_cordis_yaml_entry(const std::string &text, const McpEntry &entry);

// The insert row to paste by hand at the end of the file.
std::string cordis_yaml_snippet(const McpEntry &entry);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
