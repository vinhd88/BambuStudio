#include "AgentBridgeSimplifyHandler.hpp"

#include <cmath>
#include <limits>

#include "libslic3r/Model.hpp"
#include "libslic3r/QuadricEdgeCollapse.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeArgs.hpp"
#include "AgentBridgeGeometryChange.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

json simplify_object(const json &args)
{
    Plater    &plater = *wxGetApp().plater();
    Model     &model  = plater.model();
    const int  index  = optional_index_arg(args, "object", model.objects.size());
    if (index < 0)
        throw BridgeError(400, "invalid_args", "object is required; get_project lists them");
    ModelObject &object = *model.objects[index];
    int          part   = optional_index_arg(args, "part", object.volumes.size());
    if (part < 0) {
        if (object.volumes.size() > 1)
            throw BridgeError(400, "invalid_args", "This object has several parts; give part");
        part = 0;
    }
    ModelVolume &volume = *object.volumes[part];
    if (!volume.is_model_part())
        throw BridgeError(400, "invalid_args", "Only printed parts can be simplified");
    const double keep_pct = optional_number_arg(args, "keep_percent", 1, 99).value_or(50);
    const size_t before   = volume.mesh().its.indices.size();
    const auto   target_n = uint32_t(std::max<double>(4, std::round(before * keep_pct / 100.0)));

    json target = {{"object", index + 1}, {"part", part + 1}, {"keep_percent", keep_pct}};
    return run_geometry_change("simplify_object", target, [&](json &result) {
        indexed_triangle_set its = volume.mesh().its;
        float                max_error = std::numeric_limits<float>::max();
        its_quadric_edge_collapse(its, target_n, &max_error);
        // As the tool: paint belongs to the old triangles, so it goes with them.
        volume.supported_facets.reset();
        volume.seam_facets.reset();
        volume.mmu_segmentation_facets.reset();
        volume.fuzzy_skin_facets.reset();
        volume.set_mesh(TriangleMesh(std::move(its)));
        volume.calculate_convex_hull();
        volume.invalidate_convex_hull_2d();
        volume.set_new_unique_id();
        object.invalidate_bounding_box();
        object.ensure_on_bed();
        plater.changed_mesh(index);
        if (ObjectList *list = wxGetApp().obj_list())
            list->update_item_error_icon(index, -1);
        result["triangles_before"] = before;
        result["triangles_after"]  = volume.mesh().its.indices.size();
    });
}

}}} // namespace Slic3r::GUI::AgentBridge
