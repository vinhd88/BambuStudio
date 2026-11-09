#ifndef slic3r_AgentBridgeGeometryChange_hpp_
#define slic3r_AgentBridgeGeometryChange_hpp_

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

#include "AgentBridgeChangeJournal.hpp"

// Shared frame for tools that change geometry or layout (orient, arrange, transform, cut, ...).
// Each change is one Studio undo snapshot named "Agent <tool> <change_id>", recorded in the journal,
// so revert_change can jump the undo stack back to it. UI thread.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// Closes any open gizmo and refuses (BridgeError 409) if Studio is in a view whose undo history the
// bridge does not manage, such as the assembly view. Nothing has changed when it throws.
void prepare_for_geometry_change();

// Runs `work` as one recorded change. `work` validates nothing: check arguments before calling, so a
// refused call leaves no record. `work` fills tool-specific fields into `result`; the frame adds
// change_id, status "applied" (unless work set one; "unchanged" means Studio did nothing, and the
// record is closed so it never blocks reverting older changes) and objects (the object list afterwards,
// since numbers can shift). A failure partway is recorded and reported; revert_change can undo it.
nlohmann::json run_geometry_change(const std::string &tool, nlohmann::json target, const std::function<void(nlohmann::json &result)> &work);

// Object list after a change: number, name, instance count, part count and size in mm.
nlohmann::json objects_summary();

// revert_change for a Geometry record. Returns status reverted or refused (with reason).
nlohmann::json revert_geometry_change(const ChangeRecord &record);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
