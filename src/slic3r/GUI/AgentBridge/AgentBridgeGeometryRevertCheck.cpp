#include "AgentBridgeGeometryRevertCheck.hpp"

#include <algorithm>

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
bool is_placeholder(const UndoRedo::Snapshot &snapshot) { return snapshot.is_topmost(); }

bool is_agent_snapshot(const UndoRedo::Snapshot &snapshot) { return snapshot.name.rfind(AGENT_SNAPSHOT_PREFIX, 0) == 0; }
} // namespace

std::optional<size_t> newest_snapshot_time(const std::vector<UndoRedo::Snapshot> &snapshots)
{
    for (auto it = snapshots.rbegin(); it != snapshots.rend(); ++it)
        if (!is_placeholder(*it))
            return it->timestamp;
    return std::nullopt;
}

std::optional<std::string> geometry_revert_rejection(const std::vector<UndoRedo::Snapshot> &snapshots,
                                                     size_t                                  active_time,
                                                     const GeometrySnapshotRange            &range,
                                                     const std::vector<RedoMarker>          &redo_markers)
{
    // Studio's Undo went back into (or before) the change: it is already undone, at least in part.
    if (active_time <= range.last_time)
        return std::string("Studio's Undo has been used back past this change, so it is already undone in Studio (Redo would bring it back).");
    const auto first = std::find_if(snapshots.begin(), snapshots.end(),
                                    [&range](const UndoRedo::Snapshot &snapshot) { return snapshot.timestamp == range.first_time; });
    // Index 0 is the history's starting point, which Studio's undo cannot land on after trimming.
    if (first == snapshots.end() || first == snapshots.begin() || first->name != range.first_name)
        return std::string("Studio no longer keeps the undo history of this change (it was cleared or trimmed to save memory).");
    // The active snapshot stands for the state before its own action, so it is not counted.
    for (const UndoRedo::Snapshot &snapshot : snapshots) {
        if (snapshot.timestamp <= range.last_time || snapshot.timestamp >= active_time || is_placeholder(snapshot))
            continue;
        // Selection changes and gizmo enter/leave do not change the project, so the user clicking around is fine.
        if (UndoRedo::snapshot_modifies_project(snapshot) && !is_agent_snapshot(snapshot))
            return "The project has been changed since (\"" + snapshot.name + "\"), likely by the user; going back would undo that too.";
    }
    for (const RedoMarker &marker : redo_markers) {
        if (marker.time <= range.last_time || marker.time > active_time)
            continue;
        const bool still_there = std::any_of(snapshots.begin(), snapshots.end(),
                                             [&marker](const UndoRedo::Snapshot &s) { return s.timestamp == marker.time && s.name == marker.name; });
        if (still_there)
            return std::string("A change the agent had reverted was brought back with Redo in Studio; going back would undo it again.");
    }
    return std::nullopt;
}

}}} // namespace Slic3r::GUI::AgentBridge
