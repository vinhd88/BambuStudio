#ifndef slic3r_AgentBridgeTextHandler_hpp_
#define slic3r_AgentBridgeTextHandler_hpp_

#include <nlohmann/json.hpp>

// Tool add_text: 3D text from Studio's own text engine (libslic3r Emboss), as a new object lying on
// the plate, or raised on / engraved into the top of an object. Not the full Text tool: no surface
// wrapping, no side faces. UI thread.
namespace Slic3r { namespace GUI { namespace AgentBridge {

nlohmann::json add_text(const nlohmann::json &args);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
