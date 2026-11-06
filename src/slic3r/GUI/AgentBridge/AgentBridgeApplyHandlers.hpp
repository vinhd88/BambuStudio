#ifndef slic3r_AgentBridgeApplyHandlers_hpp_
#define slic3r_AgentBridgeApplyHandlers_hpp_

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI { namespace AgentBridge {

class ChangeJournal;

// Tool: changes settings at one level. Validates everything first: a rejected request changes nothing.
nlohmann::json apply_settings(const nlohmann::json &args);

// Outcome of the most recent change in this project, or null. For get_studio_state: the only place to
// learn the result of a change whose dialog outlasted its call.
nlohmann::json last_change_summary();

ChangeJournal &change_journal();

}}} // namespace Slic3r::GUI::AgentBridge

#endif
