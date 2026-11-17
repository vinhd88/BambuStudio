#ifndef slic3r_AgentBridgeOfficialFilamentSyncRun_hpp_
#define slic3r_AgentBridgeOfficialFilamentSyncRun_hpp_

#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "AgentBridgeFilamentSlotsApply.hpp"

// "Sync filaments from the official Studio": its user filament presets and its filament slots
// (presets and colours, i.e. its last AMS sync) into the open project, live. Used by the sidebar
// button and the sync_filaments_from_official_studio tool. UI thread.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// Why a sync cannot start, for the user: no official Studio, unreadable settings, unsaved edits.
struct FilamentSyncRefused : std::runtime_error
{
    std::string code; // official_studio_not_found, official_config_unreadable, unsaved_filament_edits
    FilamentSyncRefused(std::string code, const std::string &message) : std::runtime_error(message), code(std::move(code)) {}
};

struct FilamentSyncOutcome
{
    nlohmann::json            report; // what the sync would do (preview) or did, see run_official_filament_sync
    std::vector<FilamentSlot> before; // the slots before, for a revert
    std::vector<FilamentSlot> after;  // the slots after (preview: as planned)
};

// Preview (`apply` false) changes nothing. Applying copies the presets, reloads them, then sets the
// slots. The report: { official_printer, current_printer, printer_matches,
// presets: {added, updated, unchanged, backup?, problems?}, slot_count: {before, after},
// slots: [{slot, before, after, status: changed|same|skipped|not_applied, reason?}], mixed_slots_left_out,
// stopped? }. Throws FilamentSyncRefused.
FilamentSyncOutcome run_official_filament_sync(bool apply);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
