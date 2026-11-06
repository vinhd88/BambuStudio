#ifndef slic3r_AgentBridgeTypes_hpp_
#define slic3r_AgentBridgeTypes_hpp_

#include <functional>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

// Shared types for the agent bridge: a loopback HTTP API that lets the MCP adapter in
// tools/agent-bridge-mcp read the project open in Studio, change its settings, and slice it.
namespace Slic3r { namespace GUI { namespace AgentBridge {

struct BridgeResponse
{
    int         status = 200;
    std::string body;
    // False when the call timed out before Studio started it, so it had no effect at all.
    bool        work_ran = true;
};

// Reported to the client with a stable machine-readable code.
class BridgeError : public std::runtime_error
{
public:
    BridgeError(int status, std::string code, const std::string &message)
        : std::runtime_error(message), status(status), code(std::move(code)) {}
    int         status;
    std::string code;
};

// replace: Studio strings are not guaranteed valid UTF-8, and dump() throws on bad bytes by default.
inline std::string dump_json(const nlohmann::json &value)
{
    return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

inline BridgeResponse ok_response(const nlohmann::json &result)
{
    return {200, dump_json({{"ok", true}, {"result", result}})};
}

inline BridgeResponse error_response(int status, const std::string &code, const std::string &message)
{
    return {status, dump_json({{"ok", false}, {"error", {{"code", code}, {"message", message}}}})};
}

}}} // namespace Slic3r::GUI::AgentBridge

#endif
