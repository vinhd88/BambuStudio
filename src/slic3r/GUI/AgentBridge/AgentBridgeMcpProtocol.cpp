#include "AgentBridgeMcpProtocol.hpp"

#include <algorithm>
#include <iterator>

#include "libslic3r_version.h"

#include "AgentBridgeToolDefinitions.hpp"
#include "AgentBridgeTypes.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

const char *const MCP_PROTOCOL_VERSIONS[3] = {"2025-06-18", "2025-03-26", "2024-11-05"};

namespace {
// JSON-RPC 2.0 error codes.
constexpr int PARSE_ERROR      = -32700;
constexpr int INVALID_REQUEST  = -32600;
constexpr int METHOD_NOT_FOUND = -32601;
constexpr int INVALID_PARAMS   = -32602;

constexpr int INTERNAL_ERROR   = -32603;

// The member as a string, or empty if missing or of another type. json::value() would throw on a
// type mismatch, and one bad line must not end the helper.
std::string string_member(const json &object, const char *key)
{
    const auto it = object.is_object() ? object.find(key) : object.end();
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// JSON-RPC ids are strings or numbers; MCP also rules out null.
bool valid_id(const json &id) { return id.is_string() || id.is_number_integer() || id.is_number_unsigned(); }

std::string reply(const json &id, json result) { return dump_json({{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}}); }

std::string error_reply(const json &id, int code, const std::string &message)
{
    return dump_json({{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}});
}

json initialize_result(const json &params)
{
    // The client's version if this helper speaks it, else the newest one; the client decides
    // whether it can work with that.
    const std::string wanted  = string_member(params, "protocolVersion");
    const auto        known   = std::find(std::begin(MCP_PROTOCOL_VERSIONS), std::end(MCP_PROTOCOL_VERSIONS), wanted);
    const std::string version = known != std::end(MCP_PROTOCOL_VERSIONS) ? wanted : MCP_PROTOCOL_VERSIONS[0];
    return {{"protocolVersion", version},
            {"capabilities", {{"tools", json::object()}}},
            {"serverInfo", {{"name", "bambu-studio"}, {"version", SLIC3R_VERSION}}}};
}

bool is_known_tool(const std::string &name)
{
    const json &tools = tool_definitions();
    return std::any_of(tools.begin(), tools.end(), [&name](const json &tool) { return string_member(tool, "name") == name; });
}

std::string call_tool_reply(const json &id, const json &params, const ToolCaller &call_tool)
{
    if (!params.is_object() || !params.contains("name") || !params["name"].is_string())
        return error_reply(id, INVALID_PARAMS, "tools/call needs params.name");
    const std::string name = params["name"].get<std::string>();
    // MCP reports unknown tools as a protocol error, not as a failed call.
    if (!is_known_tool(name))
        return error_reply(id, INVALID_PARAMS, "Unknown tool: " + name);
    const json args = params.contains("arguments") && !params["arguments"].is_null() ? params["arguments"] : json::object();
    if (!args.is_object())
        return error_reply(id, INVALID_PARAMS, "tools/call arguments must be an object");

    const ToolCallOutcome outcome = call_tool(name, args);
    // Compact JSON, as the Node adapter sent: agents read it fine, and indentation only costs tokens.
    if (outcome.ok)
        return reply(id, {{"content", json::array({{{"type", "text"}, {"text", dump_json(outcome.result)}}})}});
    return reply(id, {{"isError", true}, {"content", json::array({{{"type", "text"}, {"text", outcome.code + ": " + outcome.message}}})}});
}
} // namespace

namespace {
std::optional<std::string> handle_valid_json(const json &message, const ToolCaller &call_tool)
{
    if (message.is_array())
        return error_reply(nullptr, INVALID_REQUEST, "Batches are not supported");
    const json id = message.is_object() && message.contains("id") && valid_id(message["id"]) ? message["id"] : json(nullptr);
    if (!message.is_object() || string_member(message, "jsonrpc") != "2.0")
        return error_reply(id, INVALID_REQUEST, "Not a JSON-RPC 2.0 message");
    // A response to a request this helper never sends; nothing to answer.
    if (!message.contains("method"))
        return std::nullopt;
    // Notifications (no id) never get a reply, not even an error.
    if (!message.contains("id"))
        return std::nullopt;
    if (!valid_id(message["id"]))
        return error_reply(nullptr, INVALID_REQUEST, "id must be a string or an integer");
    if (!message["method"].is_string())
        return error_reply(id, INVALID_REQUEST, "method must be a string");

    const std::string method = message["method"].get<std::string>();
    const json        params = message.contains("params") ? message["params"] : json::object();

    if (method == "initialize")
        return reply(id, initialize_result(params));
    if (method == "ping")
        return reply(id, json::object());
    if (method == "tools/list")
        return reply(id, {{"tools", tool_definitions()}});
    if (method == "tools/call")
        return call_tool_reply(id, params, call_tool);
    return error_reply(id, METHOD_NOT_FOUND, "Method not found: " + method);
}
} // namespace

std::optional<std::string> handle_mcp_message(const std::string &line, const ToolCaller &call_tool)
{
    const json message = json::parse(line, nullptr, false);
    if (message.is_discarded())
        return error_reply(nullptr, PARSE_ERROR, "Not valid JSON");
    try {
        return handle_valid_json(message, call_tool);
    } catch (const std::exception &e) {
        // Whatever slipped through, the helper answers and keeps serving.
        const json id = message.is_object() && message.contains("id") && valid_id(message["id"]) ? message["id"] : json(nullptr);
        return error_reply(id, INTERNAL_ERROR, std::string("Internal error: ") + e.what());
    }
}

}}} // namespace Slic3r::GUI::AgentBridge
