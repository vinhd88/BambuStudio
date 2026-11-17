#ifndef slic3r_AgentBridgeOfficialFilamentSync_hpp_
#define slic3r_AgentBridgeOfficialFilamentSync_hpp_

#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>

// Reads what the official Studio has in its filament slots, and copies its user filament presets
// into the agent build's folder. Only reads the official folder. GUI-free; the live apply to the
// open project is in AgentBridgeOfficialFilamentSyncApply.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// The official Studio's filament slots as it last saved them (its AMS sync lands here too).
struct OfficialFilamentSlots
{
    std::string              printer;       // its selected printer preset
    std::vector<std::string> presets;       // one filament preset name per slot
    std::vector<std::string> colours;       // "#RRGGBB" per slot; "" when the config has none
    std::vector<std::string> multi_colours; // per slot; same as colours when the config has none
    std::vector<bool>        mixed;         // a mixed-filament slot, which the sync leaves out
};

// From <official>/BambuStudio.conf. Throws std::runtime_error with a message for the user when the
// file is missing, not JSON, or has no filament list.
OfficialFilamentSlots read_official_filament_slots(const boost::filesystem::path &official_dir);

// What copying the user filament presets did (or, in a dry run, would do), by preset name.
struct FilamentPresetCopy
{
    std::vector<std::string> added;     // new in the agent folder
    std::vector<std::string> updated;   // replaced an agent preset of the same name that differed
    std::vector<std::string> unchanged; // already the same
    std::string              backup;    // where replaced files were saved first; empty if none
    std::vector<std::string> problems;  // what could not be copied, and why
};

// Copies <official>/user/<every account and default>/filament into <agent>/user/default/filament;
// an account's preset wins a name clash with default's. Replaced files are backed up first to a new
// "<agent>-backup-<time>" beside the agent folder. Account ids are removed from .info files.
// `dry_run` only reports. Never throws: problems go into the result.
FilamentPresetCopy copy_official_filament_presets(const boost::filesystem::path &official_dir, const boost::filesystem::path &agent_dir,
                                                  bool dry_run);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
