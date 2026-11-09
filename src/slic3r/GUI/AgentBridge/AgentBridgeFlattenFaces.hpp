#ifndef slic3r_AgentBridgeFlattenFaces_hpp_
#define slic3r_AgentBridgeFlattenFaces_hpp_

#include <vector>

#include <nlohmann/json.hpp>

#include "libslic3r/Point.hpp"

namespace Slic3r {
class ModelObject;
namespace GUI { namespace AgentBridge {

// A flat face of the object's convex hull it could rest on, as Studio's Lay on Face tool offers.
struct FlatFace
{
    Vec3d  normal_object;  // In the object's own coordinates; what Selection::flattening_rotate takes.
    Vec3d  normal_world;   // Direction the face points now, for the agent: [0, 0, -1] is the bottom.
    double area_mm2 = 0.;
};

// Faces of the convex hull of the object's parts, merged by normal, largest first, at most `limit`.
// Simplified from GLGizmoFlatten::update_planes: no stability filter. Areas at instance 1's scale.
std::vector<FlatFace> flat_faces(const ModelObject &object, size_t limit);

// Tool lay_on_face: turns the object so face `face` (from list_faces, or "largest") rests on the plate.
// With list_only true, only returns the faces. UI thread.
nlohmann::json lay_on_face(const nlohmann::json &args);

}} // namespace GUI::AgentBridge
} // namespace Slic3r

#endif
