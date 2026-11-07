#ifndef slic3r_AgentBridgeOfficialDataImport_hpp_
#define slic3r_AgentBridgeOfficialDataImport_hpp_

#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>
#include <nlohmann/json.hpp>

// Copies the user's printers, filaments and process presets from the official Studio's data folder
// into the agent build's own folder. Only reads the official folder. GUI-free: it runs before the
// app config and logging exist.
namespace Slic3r { namespace GUI { namespace AgentBridge {

struct ImportSummary
{
    bool                     source_found  = false; // the official folder exists
    bool                     config_copied = false; // preset selections and installed printers
    int                      user_presets  = 0;     // presets (.json files) copied into user/default
    std::vector<std::string> folders;               // profile folders copied, e.g. "system"
    std::vector<std::string> problems;              // what could not be copied, and why
    std::string              backup;                // re-import: where the agent's presets were saved first
};

// Copies allow-listed folders, user presets (moved under user/default, account ID removed) and
// app config entries. `reimport` keeps everything in the agent config outside the allow-list and
// presets that exist only in the agent folder. Never throws: problems go into the summary.
ImportSummary import_official_data(const boost::filesystem::path &official_dir, const boost::filesystem::path &agent_dir, bool reimport);

// The agent app config: `agent_config` (null on first run) with the allow-listed entries of the
// official config text replaced, and preset sync turned off. Throws if the text is not a JSON object.
nlohmann::json imported_app_config(const std::string &official_text, const nlohmann::json &agent_config);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
