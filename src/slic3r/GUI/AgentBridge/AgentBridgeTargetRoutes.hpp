#ifndef slic3r_AgentBridgeTargetRoutes_hpp_
#define slic3r_AgentBridgeTargetRoutes_hpp_

#include <string>
#include <vector>

#include "AgentBridgeApplyValidation.hpp"

// Picks the route for a validated target, for apply_settings and revert_change alike. UI thread.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// Sets values at the target level, through the same Studio handlers as editing them in the UI.
void apply_to_target(const Target &target, Preset::Type tab_type, const std::vector<SettingChange> &changes);

// Removes keys a level below global sets, so it inherits them again, as the reset button in that
// level's settings does. Presets hold every key, so at scope global there is nothing to remove.
void remove_from_target(const Target &target, const std::vector<std::string> &keys);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
