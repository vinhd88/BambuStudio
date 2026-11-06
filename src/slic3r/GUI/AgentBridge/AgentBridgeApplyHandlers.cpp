#include "AgentBridgeApplyHandlers.hpp"

#include <algorithm>
#include <iterator>

#include <boost/log/trivial.hpp>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Tab.hpp"

#include "AgentBridgeApplyRoutes.hpp"
#include "AgentBridgeApplyValidation.hpp"
#include "AgentBridgeChangeJournal.hpp"
#include "AgentBridgeMutationGate.hpp"
#include "AgentBridgeSettingsJson.hpp"
#include "AgentBridgeSliceJobs.hpp"
#include "AgentBridgeTargetRoutes.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
std::shared_ptr<ConfigOption> clone_or_null(const ConfigOption *option) { return option != nullptr ? std::shared_ptr<ConfigOption>(option->clone()) : nullptr; }

std::vector<JournalValue> before_values(const Target &target, const std::vector<SettingChange> &changes, Preset::Type tab_type)
{
    const DynamicPrintConfig &config = config_of(target, tab_type);
    std::vector<JournalValue> values;
    for (const SettingChange &change : changes) {
        const ConfigOption *before = config.option(change.key);
        values.push_back({change.key, clone_or_null(before), tab_type});
    }
    return values;
}

json value_or_null(const ConfigOption *option) { return option != nullptr ? option_to_json(*option) : json(nullptr); }

// Reports what the config holds now, not what was asked for: Studio can roll back a value through a
// dialog answer, or change linked settings along with it.
void read_back(const std::string &change_id, const Target &target, Preset::Type tab_type, const DynamicPrintConfig &before, const std::vector<SettingChange> &changes, json &result)
{
    const DynamicPrintConfig &after = config_of(target, tab_type);
    // Before and after: a handler can also remove a key, as resetting it to the inherited value does.
    std::vector<std::string> keys = after.keys();
    for (const std::string &key : before.keys())
        if (!after.has(key))
            keys.push_back(key);

    std::vector<JournalValue> linked_values;
    for (const std::string &key : keys) {
        const ConfigOption *old_value = before.option(key);
        const ConfigOption *new_value = after.option(key);
        const auto          requested = std::find_if(changes.begin(), changes.end(), [&key](const SettingChange &change) { return change.key == key; });
        if (requested != changes.end()) {
            json entry = {{"key", key}, {"before", value_or_null(old_value)}, {"after", value_or_null(new_value)}};
            result["changes"].push_back(entry);
            if (new_value == nullptr || *new_value != *requested->value)
                result["not_applied"].push_back({{"key", key}, {"requested", option_to_json(*requested->value)}, {"reason", "Studio set a different value, possibly from the answer to a dialog"}});
        } else if ((old_value == nullptr || new_value == nullptr || *old_value != *new_value) && is_user_setting(key)) {
            result["linked_changes"].push_back({{"key", key}, {"before", value_or_null(old_value)}, {"after", value_or_null(new_value)}});
            linked_values.push_back({key, clone_or_null(old_value), tab_type});
        }
    }
    change_journal().add_values(change_id, std::move(linked_values));
}
} // namespace

namespace {
ChangeJournal &journal_instance()
{
    static ChangeJournal journal;
    return journal;
}

std::string current_project_id()
{
    Plater &plater = *wxGetApp().plater();
    return std::string(plater.get_project_filename(".3mf").ToUTF8().data()) + "|" + plater.get_project_name().ToUTF8().data();
}
} // namespace

ChangeJournal &change_journal()
{
    journal_instance().set_project(current_project_id());
    return journal_instance();
}

json last_change_summary()
{
    // Called by get_studio_state, which also answers mid-load, when the name can be in flux: so it
    // only reads, and never clears records.
    const ChangeJournal &journal = journal_instance();
    const ChangeRecord  *last    = journal.last();
    if (last == nullptr || journal.project_id() != current_project_id())
        return nullptr;
    json summary = last->outcome;
    summary["change_id"] = last->change_id;
    return summary;
}

json target_identity(const Target &target)
{
    json identity = json::object();
    if (target.object != nullptr)
        identity["object_id"] = target.object->id().id;
    if (target.part != nullptr)
        identity["part_id"] = target.part->id().id;
    return identity;
}

void record_after_values(const std::string &change_id, const Target &target)
{
    ChangeRecord *record = change_journal().find(change_id);
    if (record == nullptr)
        return;
    for (JournalValue &value : record->values) {
        value.after       = clone_or_null(config_of(target, static_cast<Preset::Type>(value.tab)).option(value.key));
        value.after_known = true;
    }
}

json apply_settings(const json &args)
{
    const auto changes = args.find("changes");
    if (changes == args.end() || !changes->is_object() || changes->empty())
        throw BridgeError(400, "invalid_args", "changes must be an object of setting names and new values");

    const Target     target  = parse_target(args);
    ValidatedRequest request = validate(target, *changes);
    if (!request.rejections.empty())
        return {{"status", "rejected"}, {"rejections", std::move(request.rejections)}, {"message", "Nothing was changed."}};

    json result = {{"status", "applied"}, {"target", target.description}, {"changes", json::array()}};
    if (!request.unchanged.empty())
        result["already_set"] = request.unchanged;
    if (!request.warnings.empty())
        result["warnings"] = request.warnings;
    if (request.changes_by_tab.empty())
        return result;

    std::vector<JournalValue> values;
    for (const auto &[tab_type, tab_changes] : request.changes_by_tab) {
        auto tab_values = before_values(target, tab_changes, tab_type);
        values.insert(values.end(), std::make_move_iterator(tab_values.begin()), std::make_move_iterator(tab_values.end()));
    }
    const std::string change_id = change_journal().begin(target.description, std::move(values));
    change_journal().find(change_id)->restore = target_identity(target);
    result["change_id"]         = change_id;
    MutationScope running("change_id", change_id);

    // One undo step for the whole request, as object-level edits are undoable in Studio.
    const bool undoable = target.scope != ApplyScope::Global;
    std::unique_ptr<Plater::TakeSnapshot> snapshot;
    if (undoable)
        snapshot = std::make_unique<Plater::TakeSnapshot>(wxGetApp().plater(), "Agent settings change " + change_id);

    try {
        for (const auto &[tab_type, tab_changes] : request.changes_by_tab) {
            const DynamicPrintConfig before = config_of(target, tab_type);
            for (const SettingChange &change : tab_changes)
                BOOST_LOG_TRIVIAL(info) << "agent bridge: " << change_id << " " << target.description.dump() << " " << change.key << " = " << change.value->serialize();
            apply_to_target(target, tab_type, tab_changes);
            read_back(change_id, target, tab_type, before, tab_changes, result);
        }
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "agent bridge: " << change_id << " failed: " << e.what();
        record_after_values(change_id, target);
        change_journal().finish(change_id, {{"status", "failed"}, {"reason", e.what()}});
        note_settings_changed();
        // Some keys may have landed. The journal holds their before-values under this id.
        throw BridgeError(500, "apply_failed", "Change " + change_id + " failed partway: " + e.what() + ". Some settings may have changed; check get_settings.");
    }
    note_settings_changed();
    record_after_values(change_id, target);
    change_journal().finish(change_id, result);
    return result;
}

}}} // namespace Slic3r::GUI::AgentBridge
