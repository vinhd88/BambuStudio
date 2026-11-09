#include "AgentBridgeTransformHandlers.hpp"

#include <cmath>
#include <optional>

#include "libslic3r/Model.hpp"
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
// Reads [x, y, z] with each value in [min, max]; nullopt if absent.
std::optional<Vec3d> vec3_arg(const json &args, const char *name, double min, double max)
{
    const auto it = args.find(name);
    if (it == args.end() || it->is_null())
        return std::nullopt;
    if (!it->is_array() || it->size() != 3)
        throw BridgeError(400, "invalid_args", std::string(name) + " must be [x, y, z]");
    Vec3d out;
    for (int i = 0; i < 3; ++i) {
        const auto value = optional_number_arg(json{{name, (*it)[i]}}, name, min, max);
        if (!value)
            throw BridgeError(400, "invalid_args", std::string(name) + " must be [x, y, z] numbers");
        out[i] = *value;
    }
    return out;
}

// scale_pct: uniform; scale_pct_xyz: [x, y, z]. 100 keeps the size.
std::optional<Vec3d> scale_arg(const json &args)
{
    const auto uniform  = optional_number_arg(args, "scale_pct", 1, 10000);
    const auto per_axis = vec3_arg(args, "scale_pct_xyz", 1, 10000);
    if (uniform && per_axis)
        throw BridgeError(400, "invalid_args", "Give scale_pct or scale_pct_xyz, not both");
    if (uniform)
        return Vec3d(*uniform, *uniform, *uniform) / 100.0;
    if (per_axis)
        return *per_axis / 100.0;
    return std::nullopt;
}

std::optional<Axis> mirror_arg(const json &args)
{
    const std::string axis = string_arg(args, "mirror");
    if (axis.empty())
        return std::nullopt;
    if (axis == "x") return X;
    if (axis == "y") return Y;
    if (axis == "z") return Z;
    throw BridgeError(400, "invalid_args", "mirror must be x, y or z");
}
} // namespace

json transform_object(const json &args)
{
    Plater    &plater   = *wxGetApp().plater();
    Model     &model    = plater.model();
    const int  object   = optional_index_arg(args, "object", model.objects.size());
    if (object < 0)
        throw BridgeError(400, "invalid_args", "object is required; get_project lists them");
    const int  instance = optional_index_arg(args, "instance", model.objects[object]->instances.size());
    const auto move     = vec3_arg(args, "move_mm", -2000, 2000);
    const auto rotate   = vec3_arg(args, "rotate_deg", -360, 360);
    const auto scale    = scale_arg(args);
    const auto mirror   = mirror_arg(args);
    if (!move && !rotate && !scale && !mirror)
        throw BridgeError(400, "invalid_args", "Give at least one of move_mm, rotate_deg, scale_pct, scale_pct_xyz, mirror");

    json target = {{"object", object + 1}};
    if (instance >= 0)
        target["instance"] = instance + 1;

    return run_geometry_change("transform_object", target, [&](json &result) {
        if (instance >= 0)
            select_instance(object, instance);
        else
            select_objects({object});
        GLCanvas3D &canvas = *plater.get_view3D_canvas3D();
        Selection  &sel    = canvas.get_selection();
        // Each instance turns and scales about its own centre, as with the gizmos; empty snapshot names
        // because the change's own snapshot already covers the whole call.
        const TransformationType in_place(TransformationType::World_Relative_Independent);
        if (scale) {
            sel.setup_cache();
            sel.scale(*scale, in_place);
            canvas.do_scale("");
        }
        if (rotate) {
            sel.setup_cache();
            sel.rotate(*rotate * (M_PI / 180.0), in_place);
            canvas.do_rotate("");
        }
        if (mirror) {
            sel.setup_cache();
            sel.mirror(*mirror, TransformationType(TransformationType::World_Relative_Joint));
            canvas.do_mirror("");
        }
        if (move) {
            sel.setup_cache();
            sel.translate(*move, TransformationType(TransformationType::World_Relative_Joint));
            canvas.do_move("");
        }
        result["geometry"] = get_object_geometry({{"object", object + 1}})["instances"];
    });
}

}}} // namespace Slic3r::GUI::AgentBridge
