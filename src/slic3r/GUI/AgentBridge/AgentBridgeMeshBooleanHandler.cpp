#include "AgentBridgeMeshBooleanHandler.hpp"

#include "libslic3r/MeshBoolean.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeArgs.hpp"
#include "AgentBridgeGeometryChange.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
// The part's mesh in object coordinates.
TriangleMesh object_space_mesh(const ModelVolume &volume)
{
    TriangleMesh mesh = volume.mesh();
    mesh.transform(volume.get_matrix());
    return mesh;
}

// CGAL needs closed meshes; reports which part is not.
void require_closed(const TriangleMesh &mesh, int part)
{
    MeshBoolean::cgal::CGALMeshPtr cgal = MeshBoolean::cgal::triangle_mesh_to_cgal(mesh);
    if (!cgal || !MeshBoolean::cgal::does_bound_a_volume(*cgal))
        throw BridgeError(422, "mesh_not_closed", "Part " + std::to_string(part + 1) + " is not a closed mesh, so the boolean cannot be computed. Repair the model first.");
}
} // namespace

json mesh_boolean(const json &args)
{
    Plater    &plater = *wxGetApp().plater();
    Model     &model  = plater.model();
    const int  index  = optional_index_arg(args, "object", model.objects.size());
    if (index < 0)
        throw BridgeError(400, "invalid_args", "object is required; get_project lists them");
    ModelObject &object = *model.objects[index];
    const int    a      = optional_index_arg(args, "part_a", object.volumes.size());
    const int    b      = optional_index_arg(args, "part_b", object.volumes.size());
    if (a < 0 || b < 0 || a == b)
        throw BridgeError(400, "invalid_args", "Give two different parts part_a and part_b of the object; get_project lists them");
    if (!object.volumes[a]->is_model_part() || (!object.volumes[b]->is_model_part() && !object.volumes[b]->is_negative_volume()))
        throw BridgeError(400, "invalid_args", "part_a must be a printed part; part_b a printed part or negative volume");
    const std::string operation = string_arg(args, "operation");
    if (operation != "union" && operation != "difference" && operation != "intersection")
        throw BridgeError(400, "invalid_args", "operation must be union, difference or intersection");

    // Studio refuses deleting parts of a cut object (it would break the cut pieces' link).
    if (object.is_cut())
        throw BridgeError(409, "cut_object", "Object " + std::to_string(index + 1) + " is a piece of a cut with connectors; Studio does not change its parts.");

    json target = {{"object", index + 1}, {"part_a", a + 1}, {"part_b", b + 1}, {"operation", operation}};
    // The boolean is computed inside the change, so a long one reports in_progress; failures before the
    // model is touched are 4xx and leave nothing to revert.
    return run_geometry_change("mesh_boolean", target, [&](json &result) {
        TriangleMesh       result_mesh = object_space_mesh(*object.volumes[a]);
        const TriangleMesh other       = object_space_mesh(*object.volumes[b]);
        require_closed(result_mesh, a);
        require_closed(other, b);
        try {
            if (operation == "union")           MeshBoolean::cgal::plus(result_mesh, other);
            else if (operation == "difference") MeshBoolean::cgal::minus(result_mesh, other);
            else                                MeshBoolean::cgal::intersect(result_mesh, other);
        } catch (const std::exception &e) {
            throw BridgeError(422, "boolean_failed", std::string("The boolean failed: ") + e.what());
        }
        if (result_mesh.empty())
            throw BridgeError(422, "empty_result", "The result is empty (the parts may not overlap).");

        ModelVolume &volume = *object.volumes[a];
        // The result is in object coordinates, so the part's own transformation goes.
        volume.set_mesh(std::move(result_mesh));
        volume.set_transformation(Geometry::Transformation());
        volume.supported_facets.reset();
        volume.seam_facets.reset();
        volume.mmu_segmentation_facets.reset();
        volume.fuzzy_skin_facets.reset();
        volume.calculate_convex_hull();
        volume.invalidate_convex_hull_2d();
        volume.set_new_unique_id();
        // Studio's own delete path: maps list rows, keeps the assembly view and the object config in step.
        if (ObjectList *list = wxGetApp().obj_list())
            list->delete_from_model_and_list(std::vector<ItemForDelete>{ItemForDelete(itVolume, index, b)});
        else
            object.delete_volume(size_t(b));
        object.invalidate_bounding_box();
        object.ensure_on_bed();
        plater.changed_mesh(index);
        result["part"]  = (b < a ? a : a + 1);
        result["parts"] = object.volumes.size();
    });
}

}}} // namespace Slic3r::GUI::AgentBridge
