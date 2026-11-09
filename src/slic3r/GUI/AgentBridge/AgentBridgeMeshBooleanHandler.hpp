#ifndef slic3r_AgentBridgeMeshBooleanHandler_hpp_
#define slic3r_AgentBridgeMeshBooleanHandler_hpp_

#include <nlohmann/json.hpp>

// Tool mesh_boolean: union, difference or intersection of two parts of one object, as the Mesh
// Boolean tool does with CGAL. Part A takes the result; part B is removed. UI thread; runs synchronously.
namespace Slic3r { namespace GUI { namespace AgentBridge {

nlohmann::json mesh_boolean(const nlohmann::json &args);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
