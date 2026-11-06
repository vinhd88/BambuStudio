#ifndef slic3r_AgentBridgeFilamentHandlers_hpp_
#define slic3r_AgentBridgeFilamentHandlers_hpp_

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace Slic3r {
class ModelObject;
namespace GUI { namespace AgentBridge {

// Tool: picks a filament preset for a project slot, as choosing it in the sidebar does.
nlohmann::json select_filament(const nlohmann::json &args);
// Tool: sets which slot an object, or one of its parts, prints with.
nlohmann::json set_object_filament(const nlohmann::json &args);

// For revert_change. UI thread.

// Picks `name` for the 0-based slot through the slot's sidebar combo. Returns why not if the combo
// does not offer it. The pick itself can still be declined in a Studio dialog.
std::optional<std::string> pick_filament_for_slot(size_t slot, const std::string &name);

// The filament setting of an object and each of its parts: [{ "volume": -1 for the object or a
// 0-based part index, "extruder": slot number or null if unset }].
nlohmann::json object_filament_state(const ModelObject &object);
// Writes a state from object_filament_state back, as one undo step, and refreshes the object list.
void restore_object_filament_state(ModelObject &object, const nlohmann::json &state, const std::string &snapshot_name);

}} // namespace GUI::AgentBridge
} // namespace Slic3r

#endif
