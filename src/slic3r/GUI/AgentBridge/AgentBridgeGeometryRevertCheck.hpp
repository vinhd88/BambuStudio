#ifndef slic3r_AgentBridgeGeometryRevertCheck_hpp_
#define slic3r_AgentBridgeGeometryRevertCheck_hpp_

#include <optional>
#include <string>
#include <vector>

#include "slic3r/Utils/UndoRedo.hpp"

// Decides whether an agent geometry change can be undone by jumping Studio's undo stack back to the
// snapshot taken just before it. No GUI dependency beyond the snapshot list, so it is unit tested.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// Every snapshot the bridge takes starts with this, so a revert can tell agent snapshots from the user's.
inline const char *AGENT_SNAPSHOT_PREFIX = "Agent ";

// Snapshot range of one geometry change, from the main undo stack.
struct GeometrySnapshotRange
{
    // Snapshot taken before the change; jumping back to it undoes the change.
    size_t first_time = 0;
    // Newest snapshot when the change returned (the first one, unless Studio took more inside).
    size_t last_time  = 0;
    // Name of the first snapshot ("Agent <tool> <change_id>"). Timestamps restart when Studio clears its
    // history (new or loaded project), so the name is what proves the snapshot is still ours.
    std::string first_name;
};

// Where a Redo after an agent revert would land; see geometry_revert_rejection.
struct RedoMarker
{
    size_t      time = 0;
    std::string name;
};

// The newest snapshot that is not the topmost placeholder, or nullopt if the stack has none.
std::optional<size_t> newest_snapshot_time(const std::vector<UndoRedo::Snapshot> &snapshots);

// nullopt if the stack can go back to range.first_time without losing anyone else's work; else why not.
// `snapshots` ascend by time, as UndoRedo::Stack::snapshots() returns them; `active_time` is the active
// snapshot. The active snapshot may be below the top after newer agent changes were reverted: only
// project changes between this change and the active snapshot count; undone (redo) history does not.
// `redo_markers` are, for agent geometry changes already reverted, the snapshot a Redo would load
// (time and name, noted at revert time). Taking any new snapshot drops it; if it is still there and
// the active snapshot has reached it, the user brought that change back with Redo.
std::optional<std::string> geometry_revert_rejection(const std::vector<UndoRedo::Snapshot> &snapshots,
                                                     size_t                                  active_time,
                                                     const GeometrySnapshotRange            &range,
                                                     const std::vector<RedoMarker>          &redo_markers = {});

}}} // namespace Slic3r::GUI::AgentBridge

#endif
