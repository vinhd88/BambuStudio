#include "AgentBridgeBrimEarsHandler.hpp"

#include <algorithm>
#include <memory>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeApplyRoutes.hpp"
#include "AgentBridgeArgs.hpp"
#include "AgentBridgeGeometryChange.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
// Port of GLGizmoBrimEars::first_layer_slicer (upstream 02.09.01.70), on model data rather than
// GL volumes: the first layer outline of instance 1, in world coordinates.
ExPolygons first_layer(const ModelObject &object)
{
    MeshSlicingParamsEx params;
    params.mode           = MeshSlicingParams::SlicingMode::Regular;
    params.closing_radius = 0.1f;
    params.extra_offset   = 0.05f;
    params.resolution     = 0.01;
    const Transform3d instance = object.instances.front()->get_matrix();
    ExPolygons        parts, negatives;
    for (const ModelVolume *volume : object.volumes) {
        if (!volume->is_model_part() && !volume->is_negative_volume())
            continue;
        indexed_triangle_set its = volume->mesh().its;
        if (its.indices.empty())
            continue;
        MeshSlicingParamsEx volume_params(params);
        volume_params.trafo = params.trafo * instance * volume->get_matrix();
        if (volume_params.trafo.rotation().determinant() < 0.)
            its_flip_triangles(its);
        ExPolygons slice = slice_mesh_ex(its, std::vector<float>{0.1f}, volume_params).front();
        if (volume->is_model_part())
            parts = union_ex(parts, slice);
        else
            negatives = union_ex(negatives, slice);
    }
    return diff_ex(parts, negatives);
}

// Port of GLGizmoBrimEars::generate_points: sharp outer corners (convex) or hole corners (concave).
Points corner_points(Polygon polygon, float detection_length, float max_angle, bool is_outer)
{
    const coordf_t angle_threshold = (180 - max_angle) * PI / 180.0;
    if (detection_length > 0) {
        Points points = polygon.points;
        points.push_back(points.front());
        points = MultiPoint::_douglas_peucker(points, detection_length / SCALING_FACTOR);
        if (points.size() > 4)
            points.erase(points.end() - 1);
        polygon.points = points;
    }
    return is_outer ? polygon.convex_points(angle_threshold) : polygon.concave_points(angle_threshold);
}

// The tool's default ear size: 16 first-layer line widths across.
float default_ear_radius()
{
    const DynamicPrintConfig &config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    // A percentage line width reads as 0 here (it is relative to the nozzle), as in the tool; never below 0.5 mm.
    return std::max(0.5f, float(config.get_abs_value("initial_layer_line_width") * 16.0 / 2.0));
}

BrimPoints generate_ears(const ModelObject &object, float detection_length, float max_angle, float radius)
{
    const Transform3d to_object = object.instances.front()->get_matrix().inverse();
    BrimPoints        ears;
    const auto        add = [&](const Point &p) {
        const Vec3d world(p.x() * SCALING_FACTOR, p.y() * SCALING_FACTOR, -0.0001);
        const BrimPoint ear((to_object * world).cast<float>(), radius);
        if (std::find(ears.begin(), ears.end(), ear) == ears.end())
            ears.push_back(ear);
    };
    for (const ExPolygon &shape : first_layer(object)) {
        for (const Point &p : corner_points(shape.contour, detection_length, max_angle, true))
            add(p);
        Polygons holes = shape.holes;
        polygons_reverse(holes);
        for (const Polygon &hole : holes)
            for (const Point &p : corner_points(hole, detection_length, max_angle, false))
                add(p);
    }
    return ears;
}
} // namespace

json brim_ears(const json &args)
{
    Plater    &plater = *wxGetApp().plater();
    Model     &model  = plater.model();
    const int  index  = optional_index_arg(args, "object", model.objects.size());
    if (index < 0)
        throw BridgeError(400, "invalid_args", "object is required; get_project lists them");
    const std::string action = string_arg(args, "action", "auto");
    if (action != "auto" && action != "clear")
        throw BridgeError(400, "invalid_args", "action must be auto or clear");
    const float max_angle = float(optional_number_arg(args, "max_angle_deg", 0, 180).value_or(125));
    const float detection = float(optional_number_arg(args, "detection_mm", 0, 100).value_or(1));
    const float radius    = float(optional_number_arg(args, "ear_radius_mm", 0.5, 30).value_or(default_ear_radius()));
    const bool  set_type  = bool_arg(args, "set_brim_type", true);
    ModelObject &object   = *model.objects[index];
    if (object.instances.empty())
        throw BridgeError(409, "no_instance", "Object " + std::to_string(index + 1) + " has no instance on a plate.");

    return run_geometry_change("brim_ears", {{"object", index + 1}, {"action", action}}, [&](json &result) {
        if (action == "clear") {
            object.brim_points.clear();
        } else {
            BrimPoints ears = generate_ears(object, detection, max_angle, radius);
            if (ears.empty())
                throw BridgeError(422, "no_corners", "No corner sharper than " + std::to_string(int(max_angle)) +
                                                         " degrees on the first layer; try a larger max_angle_deg.");
            object.brim_points = std::move(ears);
            // Ears only print with brim type "painted" (btBrimEars), as the tool's warning says.
            if (set_type) {
                const DynamicPrintConfig full = wxGetApp().preset_bundle->full_config();
                const bool object_has = object.config.has("brim_type") && object.config.get().opt_enum<BrimType>("brim_type") == btBrimEars;
                const bool global_has = !object.config.has("brim_type") && full.opt_enum<BrimType>("brim_type") == btBrimEars;
                if (!object_has && !global_has) {
                    apply_to_object(object, {{"brim_type", std::make_shared<ConfigOptionEnum<BrimType>>(btBrimEars)}});
                    result["brim_type_set"] = "painted";
                }
            }
        }
        result["ears"] = object.brim_points.size();
        plater.set_plater_dirty(true);
        plater.changed_object(index);
    });
}

}}} // namespace Slic3r::GUI::AgentBridge
