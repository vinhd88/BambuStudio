#ifndef slic3r_AgentBridgeProjectJson_hpp_
#define slic3r_AgentBridgeProjectJson_hpp_

#include <nlohmann/json.hpp>

#include "libslic3r/Model.hpp"

// Converts the model to compact JSON. No GUI dependency.
// All numbers an agent can pass back (object, part, range, slot) count from 1, matching the Studio UI.
namespace Slic3r { namespace GUI { namespace AgentBridge {

nlohmann::json model_objects_to_json(const Model &model);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
