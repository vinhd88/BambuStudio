#ifndef slic3r_AgentBridgeFilamentSlotsApply_hpp_
#define slic3r_AgentBridgeFilamentSlotsApply_hpp_

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// Sets the open project's filament slots (how many, which preset, which colour) the way a user
// would in the sidebar, so Studio's own handlers keep everything else in step. Shared by the
// official-Studio filament sync and its revert. UI thread.
namespace Slic3r { namespace GUI { namespace AgentBridge {

struct FilamentSlot
{
    std::string preset;
    std::string colour;       // "#RRGGBB"; empty: leave the slot's colour alone
    std::string multi_colour; // Studio's per-slot form, colours separated by spaces; empty: same as colour
};

// The project's physical slots as they are now (mixed-filament slots are left out).
std::vector<FilamentSlot> current_filament_slots();

nlohmann::json slot_json(const FilamentSlot &slot);

// A preset Studio would offer in a slot: it exists, is visible and fits the printer. Empty if so,
// else why not.
std::string unusable_reason(const std::string &preset);

// Grows or shrinks the slot list to `target`'s size (removing from the end, where Studio asks the
// user first if objects print with a removed slot), then picks each slot's preset, then sets
// colours. A slot whose preset Studio does not offer keeps its preset. Returns the 1-based slots
// that did not end up as asked, as [{slot, reason}]; "stopped" when the list could not reach the
// asked size.
nlohmann::json apply_filament_slots(const std::vector<FilamentSlot> &target);

// Makes Studio pick up user filament presets written to <data folder>/user/default/filament:
// `updated` presets are dropped and loaded again, new ones are loaded, selections are kept.
// Refuse beforehand while the selected filament has unsaved edits. Throws on a load error.
void reload_user_filament_presets(const std::vector<std::string> &updated);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
