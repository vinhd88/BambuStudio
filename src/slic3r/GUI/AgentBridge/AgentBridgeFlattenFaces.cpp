#include "AgentBridgeFlattenFaces.hpp"

#include <algorithm>
#include <cmath>

#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Selection.hpp"

#include "AgentBridgeArgs.hpp"
#include "AgentBridgeCanvasSelection.hpp"
#include "AgentBridgeGeometryChange.hpp"
#include "AgentBridgeGeometryReadJson.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
constexpr double SAME_NORMAL = 0.001; // As the gizmo's per-component tolerance.
constexpr double MIN_AREA    = 1.0;   // mm2; the gizmo drops smaller faces too.

double round3(double v) { return std::round(v * 1000.0) / 1000.0; }
} // namespace

std::vector<FlatFace> flat_faces(const ModelObject &object, size_t limit)
{
    TriangleMesh hull;
    for (const ModelVolume *volume : object.volumes) {
        if (!volume->is_model_part())
            continue;
        TriangleMesh part = volume->get_convex_hull();
        part.transform(volume->get_matrix());
        hull.merge(part);
    }
    hull = hull.convex_hull_3d();
    const Transform3d          instance = object.instances.front()->get_matrix(true);
    const std::vector<Vec3f>   normals  = its_face_normals(hull.its);
    std::vector<FlatFace>      faces;
    for (size_t i = 0; i < hull.its.indices.size(); ++i) {
        const Vec3i &tri = hull.its.indices[i];
        Vec3d        a = instance * hull.its.vertices[tri[0]].cast<double>();
        Vec3d        b = instance * hull.its.vertices[tri[1]].cast<double>();
        Vec3d        c = instance * hull.its.vertices[tri[2]].cast<double>();
        const double area   = 0.5 * (b - a).cross(c - a).norm();
        const Vec3d  normal = normals[i].cast<double>();
        auto         same   = std::find_if(faces.begin(), faces.end(), [&](const FlatFace &f) { return (f.normal_object - normal).cwiseAbs().maxCoeff() < SAME_NORMAL; });
        if (same == faces.end()) {
            FlatFace face;
            face.normal_object = normal;
            face.normal_world  = Geometry::transform_normal(instance, normal);
            faces.push_back(face);
            same = std::prev(faces.end());
        }
        same->area_mm2 += area;
    }
    faces.erase(std::remove_if(faces.begin(), faces.end(), [](const FlatFace &f) { return f.area_mm2 < MIN_AREA; }), faces.end());
    std::sort(faces.begin(), faces.end(), [](const FlatFace &l, const FlatFace &r) { return l.area_mm2 > r.area_mm2; });
    if (faces.size() > limit)
        faces.resize(limit);
    return faces;
}

json lay_on_face(const json &args)
{
    Plater    &plater = *wxGetApp().plater();
    Model     &model  = plater.model();
    const int  index  = optional_index_arg(args, "object", model.objects.size());
    if (index < 0)
        throw BridgeError(400, "invalid_args", "object is required; get_project lists them");
    if (model.objects[index]->instances.empty())
        throw BridgeError(409, "no_instance", "Object " + std::to_string(index + 1) + " has no instance.");
    const std::vector<FlatFace> faces = flat_faces(*model.objects[index], 10);
    json listed = json::array();
    for (size_t i = 0; i < faces.size(); ++i)
        listed.push_back({{"face", i + 1},
                          {"area_mm2", std::round(faces[i].area_mm2 * 10) / 10},
                          {"points_now", {round3(faces[i].normal_world.x()), round3(faces[i].normal_world.y()), round3(faces[i].normal_world.z())}},
                          {"is_bottom_now", faces[i].normal_world.z() < -0.999}});
    if (bool_arg(args, "list_only", false))
        return {{"object", index + 1}, {"faces", std::move(listed)}};

    const int face = optional_index_arg(args, "face", faces.size());
    if (face < 0)
        throw BridgeError(400, "invalid_args", "face is required (from list_only); face 1 is the largest");
    const Vec3d normal = faces[face].normal_object.normalized();

    return run_geometry_change("lay_on_face", {{"object", index + 1}, {"face", face + 1}}, [&](json &result) {
        select_objects({index});
        GLCanvas3D &canvas = *plater.get_view3D_canvas3D();
        canvas.get_selection().setup_cache();
        canvas.do_flatten(normal, "");
        result["geometry"] = get_object_geometry({{"object", index + 1}})["instances"];
    });
}

}}} // namespace Slic3r::GUI::AgentBridge
