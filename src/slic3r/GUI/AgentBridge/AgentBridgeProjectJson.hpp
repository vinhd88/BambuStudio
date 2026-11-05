#ifndef slic3r_AgentBridgeProjectJson_hpp_
#define slic3r_AgentBridgeProjectJson_hpp_

#include <nlohmann/json.hpp>

#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Model.hpp"

// Converts the model and slice results to compact JSON. No GUI dependency.
// All numbers an agent can pass back (object, part, range, slot) count from 1, matching the Studio UI.
namespace Slic3r { namespace GUI { namespace AgentBridge {

nlohmann::json model_objects_to_json(const Model &model);

// Time, per-feature time, filament use and warnings from a valid slice result.
nlohmann::json slice_result_to_json(const GCodeProcessorResult &result);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
