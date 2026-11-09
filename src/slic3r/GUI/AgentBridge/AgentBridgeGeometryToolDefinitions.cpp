// MCP definitions of the geometry and layout tools (the 3D view toolbar), split from
// AgentBridgeToolDefinitions.cpp to keep each list readable. Add a tool here when adding it to
// AgentBridgeToolDispatch.cpp.
#include "AgentBridgeToolDefinitions.hpp"

namespace Slic3r { namespace GUI { namespace AgentBridge {

const char *geometry_tool_definitions_json()
{
    return R"tools([
  {
    "name": "get_object_geometry",
    "description": "Size and placement of one object, as Studio's position, rotation and scale fields and the Measure tool show them: per instance position_mm (relative to its plate), rotation_deg, scale_pct, bbox_min_mm/bbox_max_mm, size_mm, plate; per part facets, volume_mm3, mesh_issues, painted (supports, seam, color, fuzzy_skin); whether the object has variable layer height, brim ears or was cut. Read only.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "object": {"type": "integer", "minimum": 1}
      },
      "required": ["object"],
      "additionalProperties": false
    }
  },
  {
    "name": "orient_objects",
    "description": "Auto orient, as the toolbar button: turns objects so they print with less support and better adhesion. Give objects, or plate for every object on that plate; neither means all objects. Starts a Studio job and returns job_id: call get_job until state is done (it then lists the objects). One undo step; revert_change undoes it.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "objects": {"type": "array", "items": {"type": "integer", "minimum": 1}},
        "plate": {"type": "integer", "minimum": 1}
      },
      "additionalProperties": false
    }
  },
  {
    "name": "arrange_plate",
    "description": "Arrange, as the toolbar button: lays objects out on the plate, moving them between plates if they do not fit. plate arranges that plate only; without it all plates are arranged. spacing_mm, allow_rotation and align_to_y_axis override Studio's arrange options for this run only (0 spacing = Studio's automatic spacing). Starts a Studio job and returns job_id: call get_job until done. One undo step; revert_change undoes it.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "plate": {"type": "integer", "minimum": 1},
        "spacing_mm": {"type": "number", "minimum": 0, "maximum": 100},
        "allow_rotation": {"type": "boolean"},
        "align_to_y_axis": {"type": "boolean"}
      },
      "additionalProperties": false
    }
  },
  {
    "name": "transform_object",
    "description": "Move, rotate, scale or mirror an object (all its instances), or one instance, as the Move/Rotate/Scale tools do. All relative to now: move_mm [x, y, z] shifts; rotate_deg [x, y, z] turns about world axes around the object's centre; scale_pct (uniform) or scale_pct_xyz [x, y, z], 100 = unchanged, 50 = half size; mirror x, y or z. Applied in the order scale, rotate, mirror, move. Objects always end up resting on the plate, so a z move is undone unless the object floats on support. Returns the new placement (as get_object_geometry). One undo step; revert_change undoes it.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "object": {"type": "integer", "minimum": 1},
        "instance": {"type": "integer", "minimum": 1},
        "move_mm": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3},
        "rotate_deg": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3},
        "scale_pct": {"type": "number", "minimum": 1, "maximum": 10000, "description": "Uniform scale, 100 = unchanged."},
        "scale_pct_xyz": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3, "description": "Per-axis scale instead of scale_pct."},
        "mirror": {"type": "string", "enum": ["x", "y", "z"]}
      },
      "required": ["object"],
      "additionalProperties": false
    }
  },
  {
    "name": "split_object",
    "description": "Split an object made of separate pieces, as the toolbar's split buttons: into objects (each piece its own object; object numbers after it shift) or into parts (pieces stay one object as parts). Refused if the object is one connected piece. Returns the object list afterwards. One undo step; revert_change undoes it.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "object": {"type": "integer", "minimum": 1},
        "into": {"type": "string", "enum": ["objects", "parts"], "description": "Default objects."}
      },
      "required": ["object"],
      "additionalProperties": false
    }
  },
  {
    "name": "add_plate",
    "description": "Add an empty plate, as the toolbar button. Returns its number. One undo step; revert_change undoes it.",
    "inputSchema": {"type": "object", "properties": {}, "additionalProperties": false}
  },
  {
    "name": "add_model",
    "description": "Add the objects of a model file (.stl, .3mf, .obj, .step, .stp, .amf) to the open project, as the toolbar's Add button. Only use a path the user gave. A 3MF adds its objects only; its printer and process settings are ignored. Returns added_objects and the object list. Studio may ask the user a question (for example about units); then the call reports waiting_for_user. One undo step; revert_change undoes it.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "path": {"type": "string", "description": "Absolute path."}
      },
      "required": ["path"],
      "additionalProperties": false
    }
  }
])tools";
}

}}} // namespace Slic3r::GUI::AgentBridge
