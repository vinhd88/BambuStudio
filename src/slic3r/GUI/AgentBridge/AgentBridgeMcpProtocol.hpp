#ifndef slic3r_AgentBridgeMcpProtocol_hpp_
#define slic3r_AgentBridgeMcpProtocol_hpp_

#include <functional>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

// MCP over JSON-RPC 2.0 for the --mcp-stdio helper: initialize, ping, tools/list from the built-in
// tool definitions, and tools/call through a caller-supplied function. GUI-free, no I/O.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// What a tool call produced: the bridge's result, or a stable error code and message.
struct ToolCallOutcome
{
    bool           ok = false;
    nlohmann::json result;
    std::string    code;
    std::string    message;
};

using ToolCaller = std::function<ToolCallOutcome(const std::string &tool, const nlohmann::json &args)>;

// MCP protocol versions this helper speaks, newest first.
extern const char *const MCP_PROTOCOL_VERSIONS[3];

// Handles one JSON-RPC message (one line of stdin). Returns the reply to write, or nullopt for
// notifications and for responses the client sends, which get none.
std::optional<std::string> handle_mcp_message(const std::string &line, const ToolCaller &call_tool);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
