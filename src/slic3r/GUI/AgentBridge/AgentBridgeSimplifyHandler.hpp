#ifndef slic3r_AgentBridgeSimplifyHandler_hpp_
#define slic3r_AgentBridgeSimplifyHandler_hpp_

#include <nlohmann/json.hpp>

// Tool simplify_object: the Simplify tool's decimation (quadric edge collapse) of one part's mesh.
// Painting on the part is dropped, as the tool does without "keep paint". UI thread.
namespace Slic3r { namespace GUI { namespace AgentBridge {

nlohmann::json simplify_object(const nlohmann::json &args);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
