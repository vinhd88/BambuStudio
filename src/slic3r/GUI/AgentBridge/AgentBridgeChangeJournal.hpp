#ifndef slic3r_AgentBridgeChangeJournal_hpp_
#define slic3r_AgentBridgeChangeJournal_hpp_

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "libslic3r/Config.hpp"

// Before-values of agent changes, in memory, so a later phase can revert them. No GUI dependency.
namespace Slic3r { namespace GUI { namespace AgentBridge {

struct JournalValue
{
    std::string                   key;
    // Null if the key was not set at this level, so reverting means removing it.
    std::shared_ptr<ConfigOption> before;
};

struct ChangeRecord
{
    std::string               change_id;
    nlohmann::json            target;
    std::vector<JournalValue> values;
    // "running" until finished; then what the client was or would have been told.
    nlohmann::json            outcome;
};

class ChangeJournal
{
public:
    // Forgets everything if `project_id` differs from the last one given. A project's before-values
    // mean nothing for another project.
    void set_project(const std::string &project_id);

    // Starts a record and returns its id. Ids are never reused within a run, even across projects.
    std::string begin(nlohmann::json target, std::vector<JournalValue> values);
    // Adds before-values of keys Studio changed by itself as a result.
    void        add_values(const std::string &change_id, std::vector<JournalValue> values);
    void        finish(const std::string &change_id, nlohmann::json outcome);

    const std::deque<ChangeRecord> &records() const { return m_records; }
    const std::string              &project_id() const { return m_project_id; }
    // Null if there are none.
    const ChangeRecord             *last() const { return m_records.empty() ? nullptr : &m_records.back(); }

private:
    ChangeRecord *find(const std::string &change_id);

    std::string              m_project_id;
    std::deque<ChangeRecord> m_records;
    int                      m_next_id = 1;
};

}}} // namespace Slic3r::GUI::AgentBridge

#endif
