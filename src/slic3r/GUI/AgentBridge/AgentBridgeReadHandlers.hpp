#ifndef slic3r_AgentBridgeReadHandlers_hpp_
#define slic3r_AgentBridgeReadHandlers_hpp_

#include <string>

#include <nlohmann/json.hpp>

#include "AgentBridgeSettingsJson.hpp"

namespace Slic3r {
class Preset;
class PresetCollection;
} // namespace Slic3r

// Read-only tools. All run on the UI thread: they read Plater, PresetBundle and Model directly.
// Failures are thrown as BridgeError.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// AgentBridgeReadHandlers.cpp
nlohmann::json get_studio_state(const nlohmann::json &args);
nlohmann::json get_project(const nlohmann::json &args);
nlohmann::json get_slice_result(const nlohmann::json &args);

// AgentBridgeSettingsHandlers.cpp
nlohmann::json get_settings(const nlohmann::json &args);
nlohmann::json describe_settings(const nlohmann::json &args);

// AgentBridgePresetHandlers.cpp
nlohmann::json list_presets(const nlohmann::json &args);
nlohmann::json list_filaments(const nlohmann::json &args);
nlohmann::json get_filament(const nlohmann::json &args);
// One preset's values, compared with what the Studio UI compares it against.
nlohmann::json preset_section(PresetCollection &collection, const Preset &preset, const std::string &tab, SettingsFilter filter);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
