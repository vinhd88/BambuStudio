#ifndef slic3r_AgentBridgePresetSelection_hpp_
#define slic3r_AgentBridgePresetSelection_hpp_

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Slic3r {
class PresetCollection;
namespace GUI { namespace AgentBridge {

// Tool: switches the process preset, as picking it in the Process list does. Refuses while the
// current one has unsaved edits, unless keep_changes asks Studio to carry them over.
nlohmann::json select_process_preset(const nlohmann::json &args);

// The answer when a switch would drop unsaved edits: which preset and which keys.
nlohmann::json unsaved_changes_rejection(const PresetCollection &presets, const std::string &what);

// Unsaved edits of the selected preset as { key: serialized value }. Empty when it has none.
nlohmann::json unsaved_edits(const PresetCollection &presets);

// For revert_change: switches the process preset; with `transfer`, Studio carries unsaved edits
// over, as its Transfer button does. UI thread.
void switch_process_preset(const std::string &name, bool transfer);

// For revert_change: slots whose preset differs from `presets`, set back through the sidebar.
// Returns the slots (1-based) that could not be set back.
std::vector<int> restore_filament_slots(const std::vector<std::string> &presets);

}} // namespace GUI::AgentBridge
} // namespace Slic3r

#endif
