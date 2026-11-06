#ifndef slic3r_AgentBridgeToolDispatch_hpp_
#define slic3r_AgentBridgeToolDispatch_hpp_

#include <string>

#include "AgentBridgeTypes.hpp"

namespace Slic3r { namespace GUI { namespace AgentBridge {

// Runs one tool. Must be called on the UI thread. Failures are thrown as BridgeError.
BridgeResponse handle_tool_call(const std::string &tool, const std::string &body);

// True for tools that change the project. Any thread.
bool is_mutation_tool(const std::string &tool);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
