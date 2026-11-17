#ifndef slic3r_AgentBridgeOfficialFilamentSyncHandler_hpp_
#define slic3r_AgentBridgeOfficialFilamentSyncHandler_hpp_

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI { namespace AgentBridge {

struct ChangeRecord;

// Tool: the sidebar's "Sync filaments from official Studio" for agents. preview (default true) only
// reports what would change; preview false applies it and returns a change_id. UI thread.
nlohmann::json sync_filaments_from_official_studio(const nlohmann::json &args);

// For revert_change: puts the slots back as they were before the sync. Slots the user changed since
// are left alone and listed in not_restored. Copied preset files stay. UI thread.
nlohmann::json revert_filament_sync(const ChangeRecord &record);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
