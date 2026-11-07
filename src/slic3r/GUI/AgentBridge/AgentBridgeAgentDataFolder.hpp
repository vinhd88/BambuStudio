#ifndef slic3r_AgentBridgeAgentDataFolder_hpp_
#define slic3r_AgentBridgeAgentDataFolder_hpp_

#include <optional>
#include <string>

#include "AgentBridgeOfficialDataImport.hpp"

// The agent build's own data folder, kept apart from the official Studio's, and when to import into
// it. GUI-free: startup calls it before the app config and logging exist.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// The default agent folder beside the official one: ".../BambuStudio" -> ".../BambuStudioAgent".
std::string agent_data_dir_for(const std::string &official_dir);

// The official app's data folder, from $HOME (macOS layout), for the --mcp-stdio helper, which runs
// without wx. Empty if HOME is not set.
std::string official_data_dir_from_home();

// Startup, before the app config loads. Imports from the official folder when a re-import was
// asked for, or, if `import_if_new`, when the agent folder has no app config yet. Never touches a
// folder that is the official one. Never throws.
void prepare_agent_data_folder(const std::string &official_dir, const std::string &agent_dir, bool import_if_new);

// What the startup import did, for the notice; empty if none ran. Returns it once.
std::optional<ImportSummary> take_startup_import_summary();

// Marks a re-import for the next start. Then, before importing, Studio backs up the agent's user
// presets and app config to a new "<agent>-backup-<time>" beside the folder, and imports only if
// that worked. Throws if the mark cannot be written.
void schedule_reimport(const std::string &agent_dir);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
