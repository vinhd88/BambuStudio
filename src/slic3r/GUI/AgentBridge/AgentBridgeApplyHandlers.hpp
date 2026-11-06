#ifndef slic3r_AgentBridgeApplyHandlers_hpp_
#define slic3r_AgentBridgeApplyHandlers_hpp_

#include <exception>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI { namespace AgentBridge {

class ChangeJournal;
struct Target;
struct JournalValue;

// Tool: changes settings at one level. Validates everything first: a rejected request changes nothing.
nlohmann::json apply_settings(const nlohmann::json &args);

// Outcome of the most recent change in this project, or null. For get_studio_state: the only place to
// learn the result of a change whose dialog outlasted its call.
nlohmann::json last_change_summary();

ChangeJournal &change_journal();

// Ids of the object and part a target names, so a revert can tell whether it still finds the same
// ones by number. For global settings, the names of the presets on the touched tabs (Preset::Type
// values, as in JournalValue::tab). Empty for plates.
nlohmann::json target_identity(const Target &target, const std::set<int> &tabs);
// The tabs a change's journaled keys live on.
std::set<int> touched_tabs(const std::vector<JournalValue> &values);

// Stores what each journaled key holds now, as the values the agent left.
void record_after_values(const std::string &change_id, const Target &target);

// For a change tool whose Studio call threw: merges `after` (what the project holds now) into the
// record's restore data, marks it failed, and throws BridgeError change_failed. Without the real
// after-values the record could never be reverted, and reverts go newest first, so it would block
// every older change too.
[[noreturn]] void fail_change(const std::string &change_id, const nlohmann::json &after, const std::exception &error);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
