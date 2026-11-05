#ifndef slic3r_AgentBridgeReadHandlers_hpp_
#define slic3r_AgentBridgeReadHandlers_hpp_

#include <string>

#include <nlohmann/json.hpp>

#include "AgentBridgeTypes.hpp"

namespace Slic3r { namespace GUI { namespace AgentBridge {

// Runs one tool. Must be called on the UI thread: it reads Plater, PresetBundle and Model directly.
// Failures are thrown as BridgeError.
BridgeResponse handle_tool_call(const std::string &tool, const std::string &body);

// Tools implemented in AgentBridgeSettingsHandlers.cpp.
nlohmann::json get_settings(const nlohmann::json &args);
nlohmann::json describe_settings(const nlohmann::json &args);
nlohmann::json list_presets(const nlohmann::json &args);
nlohmann::json list_filaments(const nlohmann::json &args);
nlohmann::json get_filament(const nlohmann::json &args);

// Reads an optional 1-based number and returns it 0-based, or -1 if absent. Throws invalid_args
// if it is not a positive integer or exceeds `count`.
int optional_index_arg(const nlohmann::json &args, const char *name, size_t count);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
