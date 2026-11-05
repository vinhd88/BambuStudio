#ifndef slic3r_AgentBridgeSettingsTabMap_hpp_
#define slic3r_AgentBridgeSettingsTabMap_hpp_

#include <string>

namespace Slic3r { namespace GUI { namespace AgentBridge {

// Where a setting appears in the Studio UI. ConfigOptionDef::category covers only a fraction of the
// keys, so this comes from Tab.cpp via tools/agent-bridge-tab-map/generate-settings-tab-map.mjs.
struct SettingPlace
{
    const char *tab;   // "process", "filament" or "printer"
    const char *page;  // e.g. "Speed"
    const char *group; // e.g. "Wall speed"; may be empty
};

// Returns nullptr for keys that have no place in the UI tabs.
const SettingPlace *find_setting_place(const std::string &key);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
