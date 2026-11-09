#ifndef slic3r_AgentBridgeLayerHeightHandler_hpp_
#define slic3r_AgentBridgeLayerHeightHandler_hpp_

#include <nlohmann/json.hpp>

// Tool set_variable_layer_height: the Adaptive / Smooth / Reset buttons of Studio's variable layer
// height editor, computed with the same libslic3r functions, without opening the editor. UI thread.
namespace Slic3r { namespace GUI { namespace AgentBridge {

nlohmann::json set_variable_layer_height(const nlohmann::json &args);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
