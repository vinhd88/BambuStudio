#ifndef slic3r_AgentBridgeApplyValidation_hpp_
#define slic3r_AgentBridgeApplyValidation_hpp_

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/Preset.hpp"

#include "AgentBridgeApplyRoutes.hpp"
#include "AgentBridgeSafetyRules.hpp"

// First half of apply_settings: where a request points and whether every key in it may be applied.
// Reads Studio state but changes nothing. UI thread only.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// At scope global, the tab type that stands for the project config: the project-wide bed type
// lives there, set from the sidebar rather than a settings tab.
constexpr Preset::Type PROJECT_TAB = Preset::TYPE_PLATE;

struct Target
{
    ApplyScope            scope = ApplyScope::Global;
    nlohmann::json        description;
    PartPlate            *plate  = nullptr;
    ModelObject          *object = nullptr;
    ModelVolume          *part   = nullptr;
    t_layer_height_range  range;
    int                   filament_slot = -1;
};

struct ValidatedRequest
{
    std::map<Preset::Type, std::vector<SettingChange>> changes_by_tab;
    nlohmann::json                                     rejections = nlohmann::json::array();
    nlohmann::json                                     warnings   = nlohmann::json::array();
    std::vector<std::string>                           unchanged;
};

// Throws BridgeError if the level named does not exist.
Target parse_target(const nlohmann::json &args);

// Checks all keys, so a client hears every problem at once.
ValidatedRequest validate(const Target &target, const nlohmann::json &changes);

// Why filament settings at scope global cannot be changed for this target's slot, or nullopt. The
// Filament tab edits one slot's preset at a time.
std::optional<std::string> filament_slot_rejection(const Target &target);

// The config a change lands in. At scope global that is the tab named, or the project config.
const DynamicPrintConfig &config_of(const Target &target, Preset::Type tab_type);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
