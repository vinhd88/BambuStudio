#include "AgentBridgeGeometryReadJson.hpp"

#include <cmath>

#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeArgs.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
double round2(double value) { return std::round(value * 100.0) / 100.0; }

json vec_json(const Vec3d &v, double factor = 1.0) { return {round2(v.x() * factor), round2(v.y() * factor), round2(v.z() * factor)}; }

json instance_json(Plater &plater, const ModelObject &object, int object_index, int instance_index)
{
    const ModelInstance &instance = *object.instances[instance_index];
    PartPlateList       &plates   = plater.get_partplate_list();
    const int            plate    = plates.find_instance(object_index, instance_index);
    // Studio's position field is relative to the plate the instance is on.
    Vec3d                origin   = Vec3d::Zero();
    if (plate >= 0 && plate < plates.get_plate_count())
        origin = plates.get_plate(plate)->get_origin();
    const BoundingBoxf3 box = object.instance_bounding_box(instance_index);
    json                out = {{"instance", instance_index + 1},
                               {"position_mm", vec_json(instance.get_offset() - origin)},
                               {"rotation_deg", vec_json(instance.get_rotation(), 180.0 / M_PI)},
                               {"scale_pct", vec_json(instance.get_scaling_factor(), 100.0)},
                               {"bbox_min_mm", vec_json(box.min - origin)},
                               {"bbox_max_mm", vec_json(box.max - origin)},
                               {"size_mm", vec_json(box.size())},
                               {"printable", instance.printable}};
    if (plate >= 0)
        out["plate"] = plate + 1;
    if (instance.is_left_handed())
        out["mirrored"] = true;
    return out;
}

json part_json(const ModelVolume &volume, size_t index)
{
    const TriangleMeshStats &stats = volume.mesh().stats();
    json out = {{"part", index + 1}, {"name", volume.name}, {"facets", stats.number_of_facets}, {"volume_mm3", round2(std::abs(stats.volume))}};
    if (!stats.manifold() || stats.open_edges > 0)
        out["mesh_issues"] = {{"open_edges", stats.open_edges}, {"manifold", stats.manifold()}};
    json painted = json::array();
    if (!volume.supported_facets.empty())        painted.push_back("supports");
    if (!volume.seam_facets.empty())             painted.push_back("seam");
    if (!volume.mmu_segmentation_facets.empty()) painted.push_back("color");
    if (!volume.fuzzy_skin_facets.empty())       painted.push_back("fuzzy_skin");
    if (!painted.empty())
        out["painted"] = std::move(painted);
    if (volume.is_text())
        out["text"] = true;
    return out;
}
} // namespace

json get_object_geometry(const json &args)
{
    Plater   &plater = *wxGetApp().plater();
    Model    &model  = plater.model();
    const int index  = optional_index_arg(args, "object", model.objects.size());
    if (index < 0)
        throw BridgeError(400, "invalid_args", "object is required; get_project lists them");
    const ModelObject &object = *model.objects[index];

    json out = {{"object", index + 1}, {"name", object.name}};
    json instances = json::array();
    for (size_t i = 0; i < object.instances.size(); ++i)
        instances.push_back(instance_json(plater, object, index, int(i)));
    out["instances"] = std::move(instances);

    json   parts  = json::array();
    double volume = 0.0;
    for (size_t i = 0; i < object.volumes.size(); ++i) {
        const ModelVolume &part = *object.volumes[i];
        if (!part.is_model_part())
            continue;
        parts.push_back(part_json(part, i));
        volume += std::abs(part.mesh().stats().volume);
    }
    out["parts"]      = std::move(parts);
    // Mesh volume at the object's own scale; the printed volume scales with the instance.
    out["volume_mm3"] = round2(volume);
    out["variable_layer_height"] = !object.layer_height_profile.empty();
    if (!object.brim_points.empty())
        out["brim_ears"] = object.brim_points.size();
    if (object.is_cut())
        out["cut"] = true;
    return out;
}

}}} // namespace Slic3r::GUI::AgentBridge
