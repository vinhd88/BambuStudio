#ifndef slic3r_AgentBridgeTransformHandlers_hpp_
#define slic3r_AgentBridgeTransformHandlers_hpp_

#include <nlohmann/json.hpp>

// Tool transform_object: move, rotate, scale and mirror an object or one instance, through the same
// selection and canvas calls Studio's Move/Rotate/Scale gizmos use. One recorded change. UI thread.
namespace Slic3r { namespace GUI { namespace AgentBridge {

nlohmann::json transform_object(const nlohmann::json &args);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
