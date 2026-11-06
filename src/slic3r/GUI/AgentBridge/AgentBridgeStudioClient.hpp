#ifndef slic3r_AgentBridgeStudioClient_hpp_
#define slic3r_AgentBridgeStudioClient_hpp_

#include <chrono>
#include <string>

#include <nlohmann/json.hpp>

#include "AgentBridgeMcpProtocol.hpp"

// The --mcp-stdio helper's side of the bridge: finds the running Studio through its discovery file
// and forwards one tool call to it. Holds no state, so Studio can start, stop or reset its token
// while the AI app stays connected. GUI-free.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// Longer than the bridge's own UI timeout, so the bridge reports studio_busy before this fires.
constexpr std::chrono::seconds STUDIO_CALL_TIMEOUT(15);

// Calls `tool` on the Studio whose data folder is `data_dir`. Never throws: failures come back as
// codes the Node adapter used: not_configured, studio_not_running, discovery_unreadable,
// studio_busy, bad_response, or the bridge's own error code.
ToolCallOutcome call_running_studio(const std::string &data_dir, const std::string &tool, const nlohmann::json &args,
                                    std::chrono::milliseconds timeout = STUDIO_CALL_TIMEOUT);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
