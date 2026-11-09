// MCP definitions of the rule-based paint, text and mesh boolean tools. Add a tool here when adding
// it to AgentBridgeToolDispatch.cpp.
#include "AgentBridgeToolDefinitions.hpp"

namespace Slic3r { namespace GUI { namespace AgentBridge {

const char *paint_text_tool_definitions_json()
{
    return R"tools([
  {
    "name": "paint_facets",
    "description": "Paint an object as the painting tools do, by rule instead of brush. kind: supports (enforce or block support there), seam (enforce or block seams), color (paint with filament slot), fuzzy_skin (enforce). select: all, overhangs (surfaces facing down within angle_deg of horizontal; default the process support threshold angle), or facing (surfaces facing top, bottom, front, back, left or right within angle_deg, default 45). z_from_mm / z_to_mm limit it to a height band (facet centre, above the plate). state: enforce (default), block, or none to erase; color uses slot instead. Adds to existing paint; action clear removes all paint of that kind. Paints whole mesh triangles, so it is as coarse as the mesh. Painted supports and colors take effect when sliced (support enforcers need support of a suitable type). part limits it to one part. One undo step; revert_change undoes it.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "object": {"type": "integer", "minimum": 1},
        "part": {"type": "integer", "minimum": 1},
        "kind": {"type": "string", "enum": ["supports", "seam", "color", "fuzzy_skin"]},
        "action": {"type": "string", "enum": ["paint", "clear"]},
        "select": {"type": "string", "enum": ["all", "overhangs", "facing"]},
        "facing": {"type": "string", "enum": ["top", "bottom", "front", "back", "left", "right"]},
        "angle_deg": {"type": "number", "minimum": 1, "maximum": 90},
        "z_from_mm": {"type": "number"},
        "z_to_mm": {"type": "number"},
        "state": {"type": "string", "enum": ["enforce", "block", "none"]},
        "slot": {"type": "integer", "minimum": 1}
      },
      "required": ["object", "kind"],
      "additionalProperties": false
    }
  },
  {
    "name": "add_text",
    "description": "3D text with Studio's text engine. mode new_object (default) adds the text lying on the plate as its own object; emboss_top raises it on the top of object; engrave_top cuts it into the top of object (as a negative part). Centred on the object's footprint at its highest point, so the top should be flat there. size_mm is the letter height (default 10), depth_mm the thickness or engraving depth (default 1.5). font_path: a .ttf/.otf the user named; default Studio's bundled font. Not the full Text tool: no text on side faces or curved surfaces. One undo step; revert_change undoes it.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "text": {"type": "string"},
        "mode": {"type": "string", "enum": ["new_object", "emboss_top", "engrave_top"]},
        "object": {"type": "integer", "minimum": 1},
        "size_mm": {"type": "number", "minimum": 2, "maximum": 200},
        "depth_mm": {"type": "number", "minimum": 0.2, "maximum": 50},
        "font_path": {"type": "string"}
      },
      "required": ["text"],
      "additionalProperties": false
    }
  },
  {
    "name": "mesh_boolean",
    "description": "Union, difference or intersection of two parts of one object, as the Mesh Boolean tool. part_a keeps the result; part_b (a part or negative volume) is removed. Both must be closed meshes. Painting on part_a is removed. Part numbers after part_b shift; the result gives part_a's new number. One undo step; revert_change undoes it.",
    "inputSchema": {
      "type": "object",
      "properties": {
        "object": {"type": "integer", "minimum": 1},
        "part_a": {"type": "integer", "minimum": 1},
        "part_b": {"type": "integer", "minimum": 1},
        "operation": {"type": "string", "enum": ["union", "difference", "intersection"]}
      },
      "required": ["object", "part_a", "part_b", "operation"],
      "additionalProperties": false
    }
  }
])tools";
}

}}} // namespace Slic3r::GUI::AgentBridge
