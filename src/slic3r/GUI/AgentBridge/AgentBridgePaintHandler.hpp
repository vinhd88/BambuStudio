#ifndef slic3r_AgentBridgePaintHandler_hpp_
#define slic3r_AgentBridgePaintHandler_hpp_

#include <nlohmann/json.hpp>

// Tool paint_facets: the painting tools (support, seam, color, fuzzy skin) driven by rules instead of
// a brush: overhangs steeper than an angle, faces facing a direction, a height band, or everything.
// Paints whole triangles of the mesh, so it is as fine as the mesh. UI thread.
namespace Slic3r { namespace GUI { namespace AgentBridge {

nlohmann::json paint_facets(const nlohmann::json &args);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
