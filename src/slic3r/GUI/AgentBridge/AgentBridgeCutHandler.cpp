#include "AgentBridgeCutHandler.hpp"

#include "libslic3r/CutUtils.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeArgs.hpp"
#include "AgentBridgeGeometryChange.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
ModelObjectCutAttributes attributes_for(bool keep_upper, bool keep_lower, bool flip_upper, bool to_parts)
{
    // No connectors and no cut info, as the tool does for a first plain plane cut: the halves are
    // ordinary objects afterwards.
    return only_if(keep_upper, ModelObjectCutAttribute::KeepUpper) | only_if(keep_lower, ModelObjectCutAttribute::KeepLower) |
           only_if(to_parts, ModelObjectCutAttribute::CutToParts) | only_if(flip_upper, ModelObjectCutAttribute::FlipUpper) |
           ModelObjectCutAttribute::InvalidateCutInfo;
}
} // namespace

json cut_object(const json &args)
{
    Plater    &plater = *wxGetApp().plater();
    Model     &model  = plater.model();
    const int  index  = optional_index_arg(args, "object", model.objects.size());
    if (index < 0)
        throw BridgeError(400, "invalid_args", "object is required; get_project lists them");
    ModelObject &object   = *model.objects[index];
    const int    instance = std::max(0, optional_index_arg(args, "instance", object.instances.size()));
    if (object.instances.empty())
        throw BridgeError(409, "no_instance", "Object " + std::to_string(index + 1) + " has no instance.");
    const auto height = optional_number_arg(args, "height_mm", 0, 10000);
    if (!height)
        throw BridgeError(400, "invalid_args", "height_mm is required: the cut height above the plate");
    const BoundingBoxf3 box = object.instance_bounding_box(instance);
    if (*height <= box.min.z() + 0.01 || *height >= box.max.z() - 0.01)
        throw BridgeError(400, "invalid_args", "height_mm must be inside the object, between " + json(box.min.z()).dump() + " and " + json(box.max.z()).dump() + " mm");
    const std::string keep = string_arg(args, "keep", "both");
    if (keep != "both" && keep != "upper" && keep != "lower")
        throw BridgeError(400, "invalid_args", "keep must be both, upper or lower");
    const bool flip_upper = bool_arg(args, "flip_upper", false);
    const bool to_parts   = bool_arg(args, "as_parts", false);
    if (to_parts && keep != "both")
        throw BridgeError(400, "invalid_args", "as_parts needs keep both");
    // Pieces of a Studio cut with connectors share cut info; recutting them needs the Cut tool's bookkeeping.
    if (object.is_cut())
        throw BridgeError(409, "cut_object", "Object " + std::to_string(index + 1) + " is a piece of an earlier cut with connectors; use Studio's Cut tool for it.");
    if (!object.cut_connectors.empty())
        throw BridgeError(409, "has_connectors", "The object has cut connectors set up in the Cut tool; ask the user to finish or clear them.");

    json target = {{"object", index + 1}, {"height_mm", *height}, {"keep", keep}};
    return run_geometry_change("cut_object", target, [&](json &result) {
        // The tool's cut matrix: the plane centre relative to the instance offset, with no rotation.
        const Vec3d       offset = object.instances[instance]->get_offset();
        const Transform3d matrix = Geometry::translation_transform(Vec3d(0., 0., *height - offset.z()));
        Cut cut(&object, instance, matrix, attributes_for(keep != "lower", keep != "upper", flip_upper, to_parts));
        cut.set_offset_for_two_part = true;
        const ModelObjectPtrs &pieces = cut.perform_with_plane();
        if (cut.was_canceled() || pieces.empty())
            throw BridgeError(422, "cut_failed", "The cut produced nothing; the plane may miss the mesh.");
        result["pieces"] = pieces.size();
        plater.apply_cut_object_to_model(size_t(index), pieces);
    });
}

}}} // namespace Slic3r::GUI::AgentBridge
