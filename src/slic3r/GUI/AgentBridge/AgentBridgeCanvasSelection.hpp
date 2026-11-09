#ifndef slic3r_AgentBridgeCanvasSelection_hpp_
#define slic3r_AgentBridgeCanvasSelection_hpp_

#include <vector>

#include <nlohmann/json.hpp>

// Selecting objects in the 3D view the way a click does, for Studio operations that act on the
// selection (orient, split, transform). UI thread.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// Reads an optional array of 1-based object numbers; returns 0-based indexes, empty if absent.
// Throws invalid_args / not_found like optional_index_arg.
std::vector<int> object_list_arg(const nlohmann::json &args, const char *name);

// Replaces the selection with these objects (all their instances). Empty selects nothing.
void select_objects(const std::vector<int> &object_indexes);
// Replaces the selection with one instance.
void select_instance(int object_index, int instance_index);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
