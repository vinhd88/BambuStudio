#include "AgentBridgePaintHandler.hpp"

#include <cmath>
#include <optional>

#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeArgs.hpp"
#include "AgentBridgeGeometryChange.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
// Which facets a rule picks, in world terms (instance 1).
struct PaintRule
{
    std::string           select;          // all, overhangs, facing
    double                overhang_cos = 0; // overhangs: normal z below -cos(angle)
    Vec3d                 direction    = Vec3d::Zero();
    double                facing_cos   = 0;
    std::optional<double> z_from, z_to;    // optional band on the facet centre's height
};

FacetsAnnotation &annotation(ModelVolume &volume, const std::string &kind)
{
    if (kind == "supports")   return volume.supported_facets;
    if (kind == "seam")       return volume.seam_facets;
    if (kind == "color")      return volume.mmu_segmentation_facets;
    return volume.fuzzy_skin_facets;
}

Vec3d direction_of(const std::string &facing)
{
    if (facing == "top")    return Vec3d::UnitZ();
    if (facing == "bottom") return -Vec3d::UnitZ();
    if (facing == "back")   return Vec3d::UnitY();
    if (facing == "front")  return -Vec3d::UnitY();
    if (facing == "right")  return Vec3d::UnitX();
    if (facing == "left")   return -Vec3d::UnitX();
    throw BridgeError(400, "invalid_args", "facing must be top, bottom, front, back, left or right");
}

bool picks(const PaintRule &rule, const Vec3d &normal, double centre_z)
{
    if (rule.z_from && centre_z < *rule.z_from) return false;
    if (rule.z_to && centre_z > *rule.z_to)     return false;
    if (rule.select == "overhangs") return normal.z() < -rule.overhang_cos;
    if (rule.select == "facing")    return normal.dot(rule.direction) >= rule.facing_cos;
    return true;
}

// Paints the picked facets of one part on top of its existing paint. Returns how many were painted.
size_t paint_volume(ModelVolume &volume, const Transform3d &instance, const std::string &kind, const PaintRule &rule, EnforcerBlockerType state)
{
    const TriangleMesh &mesh   = volume.mesh();
    const Transform3d   world  = instance * volume.get_matrix();
    const auto          normals = its_face_normals(mesh.its);
    FacetsAnnotation   &facets = annotation(volume, kind);
    TriangleSelector    selector(mesh);
    selector.deserialize(facets.get_data());
    size_t painted = 0;
    for (size_t i = 0; i < mesh.its.indices.size(); ++i) {
        const Vec3i &tri    = mesh.its.indices[i];
        const Vec3d  centre = world * ((mesh.its.vertices[tri[0]] + mesh.its.vertices[tri[1]] + mesh.its.vertices[tri[2]]) / 3.f).cast<double>();
        const Vec3d  normal = Geometry::transform_normal(world, normals[i]).normalized();
        if (picks(rule, normal, centre.z())) {
            selector.set_facet(int(i), state);
            ++painted;
        }
    }
    facets.set(selector);
    return painted;
}

PaintRule read_rule(const json &args)
{
    PaintRule rule;
    rule.select = string_arg(args, "select", "all");
    if (rule.select != "all" && rule.select != "overhangs" && rule.select != "facing")
        throw BridgeError(400, "invalid_args", "select must be all, overhangs or facing");
    if (rule.select == "overhangs") {
        // Default: the process's support threshold angle, as the tool's "highlight overhangs".
        const auto &config  = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        double      fallback = config.has("support_threshold_angle") ? config.opt_int("support_threshold_angle") : 30;
        if (fallback <= 0) fallback = 30;
        rule.overhang_cos = std::cos(Geometry::deg2rad(optional_number_arg(args, "angle_deg", 1, 89).value_or(fallback)));
    }
    if (rule.select == "facing") {
        rule.direction  = direction_of(string_arg(args, "facing"));
        rule.facing_cos = std::cos(Geometry::deg2rad(optional_number_arg(args, "angle_deg", 1, 90).value_or(45)));
    }
    rule.z_from = optional_number_arg(args, "z_from_mm", -1000, 10000);
    rule.z_to   = optional_number_arg(args, "z_to_mm", -1000, 10000);
    return rule;
}

EnforcerBlockerType read_state(const json &args, const std::string &kind)
{
    const std::string state = string_arg(args, "state", kind == "color" ? "" : "enforce");
    if (state == "none")
        return EnforcerBlockerType::NONE;
    if (kind == "color") {
        const int slot = optional_index_arg(args, "slot", wxGetApp().preset_bundle->filament_presets.size());
        if (slot < 0)
            throw BridgeError(400, "invalid_args", "color needs slot: the filament to paint with (or state none to erase)");
        return EnforcerBlockerType(slot + 1);
    }
    if (state == "enforce") return EnforcerBlockerType::ENFORCER;
    if (state == "block" && kind != "fuzzy_skin") return EnforcerBlockerType::BLOCKER;
    throw BridgeError(400, "invalid_args", kind == "fuzzy_skin" ? "fuzzy_skin state must be enforce or none" : "state must be enforce, block or none");
}
} // namespace

json paint_facets(const json &args)
{
    Plater    &plater = *wxGetApp().plater();
    Model     &model  = plater.model();
    const int  index  = optional_index_arg(args, "object", model.objects.size());
    if (index < 0)
        throw BridgeError(400, "invalid_args", "object is required; get_project lists them");
    ModelObject &object = *model.objects[index];
    if (object.instances.empty())
        throw BridgeError(409, "no_instance", "Object " + std::to_string(index + 1) + " has no instance.");
    const std::string kind = string_arg(args, "kind");
    if (kind != "supports" && kind != "seam" && kind != "color" && kind != "fuzzy_skin")
        throw BridgeError(400, "invalid_args", "kind must be supports, seam, color or fuzzy_skin");
    const int         part   = optional_index_arg(args, "part", object.volumes.size());
    const std::string action = string_arg(args, "action", "paint");
    if (action != "paint" && action != "clear")
        throw BridgeError(400, "invalid_args", "action must be paint or clear");
    const PaintRule           rule  = action == "paint" ? read_rule(args) : PaintRule();
    const EnforcerBlockerType state = action == "paint" ? read_state(args, kind) : EnforcerBlockerType::NONE;

    json target = {{"object", index + 1}, {"kind", kind}, {"action", action}};
    return run_geometry_change("paint_facets", target, [&](json &result) {
        const Transform3d instance = object.instances.front()->get_matrix();
        size_t            painted  = 0;
        for (size_t i = 0; i < object.volumes.size(); ++i) {
            ModelVolume &volume = *object.volumes[i];
            if (!volume.is_model_part() || (part >= 0 && int(i) != part))
                continue;
            if (action == "clear")
                annotation(volume, kind).reset();
            else
                painted += paint_volume(volume, instance, kind, rule, state);
        }
        if (action == "paint")
            result["facets_painted"] = painted;
        if (ObjectList *list = wxGetApp().obj_list())
            list->update_info_items(size_t(index));
        plater.set_plater_dirty(true);
        plater.changed_object(index);
    });
}

}}} // namespace Slic3r::GUI::AgentBridge
