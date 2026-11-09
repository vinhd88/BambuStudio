#ifndef slic3r_AgentBridgeChangeJournal_hpp_
#define slic3r_AgentBridgeChangeJournal_hpp_

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "libslic3r/Config.hpp"

// Before- and after-values of agent changes, in memory, so revert_change can undo them and tell
// whether the user has changed anything since. No GUI dependency.
namespace Slic3r { namespace GUI { namespace AgentBridge {

struct JournalValue
{
    std::string                   key;
    // Null if the key was not set at this level, so reverting means removing it.
    std::shared_ptr<ConfigOption> before;
    // Preset::Type of the tab the key lives on at scope global; 0 when the scope has a single config.
    int                           tab = 0;
    // What the agent left. Null if the key was not set afterwards. Filled when the change finishes.
    std::shared_ptr<ConfigOption> after;
    bool                          after_known = false;
};

// What a record changed, which decides how revert_change undoes it.
// Geometry: layout or mesh changes, undone through Studio's undo stack (restore holds the snapshot range).
enum class ChangeKind { Settings, FilamentPreset, ProcessPreset, ObjectFilament, Geometry };

struct ChangeRecord
{
    std::string               change_id;
    ChangeKind                kind = ChangeKind::Settings;
    // The tool arguments that name the level changed, so a revert can find it again.
    nlohmann::json            target;
    std::vector<JournalValue> values;
    // Data a revert needs for kinds other than Settings, such as preset names before and after.
    nlohmann::json            restore = nlohmann::json::object();
    // "running" until finished; then what the client was or would have been told.
    nlohmann::json            outcome;
    bool                      reverted = false;
};

class ChangeJournal
{
public:
    // Forgets everything if `project_id` differs from the last one given. A project's before-values
    // mean nothing for another project.
    void set_project(const std::string &project_id);

    // Starts a record and returns its id. Ids are never reused within a run, even across projects.
    std::string begin(nlohmann::json target, std::vector<JournalValue> values, ChangeKind kind = ChangeKind::Settings);
    // Adds before-values of keys Studio changed by itself as a result.
    void        add_values(const std::string &change_id, std::vector<JournalValue> values);
    void        finish(const std::string &change_id, nlohmann::json outcome);

    // Null if no record has this id, for instance because it was dropped as too old.
    ChangeRecord *find(const std::string &change_id);
    // The newest record not yet reverted, or null. Reverts go newest first, so this is the only one
    // revert_change may undo.
    ChangeRecord *newest_active();
    void          mark_reverted(const std::string &change_id);

    const std::deque<ChangeRecord> &records() const { return m_records; }
    const std::string              &project_id() const { return m_project_id; }
    // Null if there are none.
    const ChangeRecord             *last() const { return m_records.empty() ? nullptr : &m_records.back(); }

private:
    std::string              m_project_id;
    std::deque<ChangeRecord> m_records;
    int                      m_next_id = 1;
};

}}} // namespace Slic3r::GUI::AgentBridge

#endif
