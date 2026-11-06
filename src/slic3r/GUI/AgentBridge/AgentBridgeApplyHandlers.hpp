#ifndef slic3r_AgentBridgeApplyHandlers_hpp_
#define slic3r_AgentBridgeApplyHandlers_hpp_

#include <string>

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI { namespace AgentBridge {

class ChangeJournal;
struct Target;

// Tool: changes settings at one level. Validates everything first: a rejected request changes nothing.
nlohmann::json apply_settings(const nlohmann::json &args);

// Outcome of the most recent change in this project, or null. For get_studio_state: the only place to
// learn the result of a change whose dialog outlasted its call.
nlohmann::json last_change_summary();

ChangeJournal &change_journal();

// Ids of the object and part a target names, so a revert can tell whether it still finds the same
// ones by number. Empty for plates and global settings.
nlohmann::json target_identity(const Target &target);

// Stores what each journaled key holds now, as the values the agent left.
void record_after_values(const std::string &change_id, const Target &target);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
