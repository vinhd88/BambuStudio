// MCP definitions of the tools that change an object's shape or print aids (variable layer height,
// cut, brim ears, lay on face, simplify). Add a tool here when adding it to AgentBridgeToolDispatch.cpp.
#include "AgentBridgeToolDefinitions.hpp"

namespace Slic3r { namespace GUI { namespace AgentBridge {

const char *mesh_tool_definitions_json()
{
    return R"tools([
  {
    "name": "set_variable_layer_height",
    "description": "Variable layer height for one object, as the editor's buttons: adaptive makes layers thin where the surface curves and thick where it is steep (quality 0 = finest, 1 = fastest, Studio default 0.5); smooth evens out an existing profile (smooth_radius 1-10, keep_min keeps the thinnest layers); reset goes back to the process layer height. Layer heights stay within the printer's min/max layer height. Re-slice to see time and quality change. Refused while the editor is open in Studio. One undo step; revert_change undoes it.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "object": {"type": "integer", "minimum": 1},
        "mode": {"type": "string", "enum": ["adaptive", "smooth", "reset"]},
        "quality": {"type": "number", "minimum": 0, "maximum": 1},
        "smooth_radius": {"type": "integer", "minimum": 1, "maximum": 10},
        "keep_min": {"type": "boolean"}
      },
      "required": ["object", "mode"],
      "additionalProperties": false
    }
  },
  {
    "name": "cut_object",
    "description": "Cut an object with a horizontal plane at height_mm above the plate, as the Cut tool's plane cut (no connectors). keep both (default), upper or lower; flip_upper turns the upper piece over onto its cut face; as_parts keeps both pieces as parts of one object. The pieces become new objects, so object numbers after it shift; the result lists them. Get the height range from get_object_geometry. One undo step; revert_change undoes it.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "object": {"type": "integer", "minimum": 1},
        "instance": {"type": "integer", "minimum": 1},
        "height_mm": {"type": "number", "minimum": 0},
        "keep": {"type": "string", "enum": ["both", "upper", "lower"]},
        "flip_upper": {"type": "boolean"},
        "as_parts": {"type": "boolean"}
      },
      "required": ["object", "height_mm"],
      "additionalProperties": false
    }
  },
  {
    "name": "brim_ears",
    "description": "Brim ears (small round brim pads at sharp corners, against warping and corners lifting), as the Brim Ears tool. action auto places ears at first-layer corners sharper than max_angle_deg (default 125) with ear_radius_mm (default 16 first-layer line widths across), and by default sets the object's brim type to painted so they print (set_brim_type false to skip); clear deletes all ears. One undo step; revert_change undoes it.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "object": {"type": "integer", "minimum": 1},
        "action": {"type": "string", "enum": ["auto", "clear"]},
        "max_angle_deg": {"type": "number", "minimum": 0, "maximum": 180},
        "detection_mm": {"type": "number", "minimum": 0, "maximum": 100, "description": "Outline simplification before corner detection; default 1."},
        "ear_radius_mm": {"type": "number", "minimum": 0.5, "maximum": 30},
        "set_brim_type": {"type": "boolean"}
      },
      "required": ["object"],
      "additionalProperties": false
    }
  },
  {
    "name": "lay_on_face",
    "description": "Lay on Face: turn an object so one of its flat faces rests on the plate. First call with list_only true to get up to 10 faces (largest first) with area_mm2, points_now (direction the face points now) and is_bottom_now; then call with face. A large flat bottom face improves adhesion and needs less support. One undo step; revert_change undoes it.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "object": {"type": "integer", "minimum": 1},
        "face": {"type": "integer", "minimum": 1},
        "list_only": {"type": "boolean"}
      },
      "required": ["object"],
      "additionalProperties": false
    }
  },
  {
    "name": "simplify_object",
    "description": "Reduce a part's triangle count, as the Simplify tool: keep_percent of the triangles (default 50). Fine detail can soften; painting on that part is removed. part is required for objects with several parts. One undo step; revert_change undoes it.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "object": {"type": "integer", "minimum": 1},
        "part": {"type": "integer", "minimum": 1},
        "keep_percent": {"type": "number", "minimum": 1, "maximum": 99}
      },
      "required": ["object"],
      "additionalProperties": false
    }
  }
])tools";
}

}}} // namespace Slic3r::GUI::AgentBridge
