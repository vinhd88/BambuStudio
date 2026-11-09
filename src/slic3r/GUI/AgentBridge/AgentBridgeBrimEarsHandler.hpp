#ifndef slic3r_AgentBridgeBrimEarsHandler_hpp_
#define slic3r_AgentBridgeBrimEarsHandler_hpp_

#include <nlohmann/json.hpp>

// Tool brim_ears: the Brim Ears tool's "Auto-generate" and "Delete all", without opening it.
// Auto places ears at the sharp corners of the object's first layer, as the tool does. UI thread.
namespace Slic3r { namespace GUI { namespace AgentBridge {

nlohmann::json brim_ears(const nlohmann::json &args);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
