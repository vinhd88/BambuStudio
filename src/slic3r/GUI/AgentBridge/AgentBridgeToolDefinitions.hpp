#ifndef slic3r_AgentBridgeToolDefinitions_hpp_
#define slic3r_AgentBridgeToolDefinitions_hpp_

#include <nlohmann/json.hpp>

// The bridge's tools as MCP describes them. GUI-free.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// Array of { name, description, inputSchema }, in the order tools/list returns them.
const nlohmann::json &tool_definitions();

// JSON array text of the geometry and layout tools; part of tool_definitions().
const char *geometry_tool_definitions_json();
// JSON array text of the shape and print-aid tools; part of tool_definitions().
const char *mesh_tool_definitions_json();
// JSON array text of the paint, text and mesh boolean tools; part of tool_definitions().
const char *paint_text_tool_definitions_json();

}}} // namespace Slic3r::GUI::AgentBridge

#endif
