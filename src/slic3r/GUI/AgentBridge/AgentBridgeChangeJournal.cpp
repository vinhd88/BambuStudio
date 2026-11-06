#include "AgentBridgeChangeJournal.hpp"

#include <algorithm>

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
// An agent tuning settings makes hundreds of changes at most in a session.
constexpr size_t MAX_RECORDS = 500;
} // namespace

void ChangeJournal::set_project(const std::string &project_id)
{
    if (project_id != m_project_id)
        m_records.clear();
    m_project_id = project_id;
}

std::string ChangeJournal::begin(nlohmann::json target, std::vector<JournalValue> values)
{
    if (m_records.size() >= MAX_RECORDS)
        m_records.pop_front();
    ChangeRecord record;
    record.change_id = "c" + std::to_string(m_next_id++);
    record.target    = std::move(target);
    record.values    = std::move(values);
    record.outcome   = {{"status", "running"}};
    m_records.push_back(std::move(record));
    return m_records.back().change_id;
}

ChangeRecord *ChangeJournal::find(const std::string &change_id)
{
    const auto it = std::find_if(m_records.begin(), m_records.end(), [&change_id](const ChangeRecord &record) { return record.change_id == change_id; });
    return it == m_records.end() ? nullptr : &*it;
}

void ChangeJournal::add_values(const std::string &change_id, std::vector<JournalValue> values)
{
    if (ChangeRecord *record = find(change_id))
        record->values.insert(record->values.end(), std::make_move_iterator(values.begin()), std::make_move_iterator(values.end()));
}

void ChangeJournal::finish(const std::string &change_id, nlohmann::json outcome)
{
    if (ChangeRecord *record = find(change_id))
        record->outcome = std::move(outcome);
}

}}} // namespace Slic3r::GUI::AgentBridge
