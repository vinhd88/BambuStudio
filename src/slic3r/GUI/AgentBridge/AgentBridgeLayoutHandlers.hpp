#ifndef slic3r_AgentBridgeLayoutHandlers_hpp_
#define slic3r_AgentBridgeLayoutHandlers_hpp_

#include <nlohmann/json.hpp>

// Tools for the left part of the 3D view toolbar: add model, add plate, auto orient, arrange, and
// split to objects / parts. Each is one recorded change (AgentBridgeGeometryChange). UI thread.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// Starts Studio's auto orient job; returns a job id for get_job.
nlohmann::json orient_objects(const nlohmann::json &args);
// Starts Studio's arrange job; returns a job id for get_job.
nlohmann::json arrange_plate(const nlohmann::json &args);
nlohmann::json add_plate(const nlohmann::json &args);
nlohmann::json add_model(const nlohmann::json &args);
nlohmann::json split_object(const nlohmann::json &args);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
