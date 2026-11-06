#ifndef slic3r_AgentBridgeToolDefinitions_hpp_
#define slic3r_AgentBridgeToolDefinitions_hpp_

#include <nlohmann/json.hpp>

// The bridge's tools as MCP describes them. GUI-free.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// Array of { name, description, inputSchema }, in the order tools/list returns them.
const nlohmann::json &tool_definitions();

}}} // namespace Slic3r::GUI::AgentBridge

#endif
