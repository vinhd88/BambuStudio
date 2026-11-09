#ifndef slic3r_AgentBridgeGeometryReadJson_hpp_
#define slic3r_AgentBridgeGeometryReadJson_hpp_

#include <nlohmann/json.hpp>

// Tool get_object_geometry: what Studio's Measure tool and position/rotation/scale fields show, as
// numbers: size, placement, mesh volume and health, and which paint and layer-height data exist.
// Read only. UI thread.
namespace Slic3r { namespace GUI { namespace AgentBridge {

nlohmann::json get_object_geometry(const nlohmann::json &args);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
