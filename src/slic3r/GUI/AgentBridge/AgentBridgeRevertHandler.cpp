#include "AgentBridgeRevertHandler.hpp"

#include <map>
#include <memory>

#include <boost/log/trivial.hpp>

#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeApplyHandlers.hpp"
#include "AgentBridgeArgs.hpp"
#include "AgentBridgeChangeJournal.hpp"
#include "AgentBridgeFilamentHandlers.hpp"
#include "AgentBridgeMutationGate.hpp"
#include "AgentBridgePresetSelection.hpp"
#include "AgentBridgeSettingsJson.hpp"
#include "AgentBridgeSliceJobs.hpp"
#include "AgentBridgeTargetRoutes.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
PresetBundle &bundle() { return *wxGetApp().preset_bundle; }

bool same(const ConfigOption *a, const ConfigOption *b) { return a == nullptr || b == nullptr ? a == b : *a == *b; }

json refused(const std::string &reason, json changed = nullptr)
{
    json out = {{"status", "refused"}, {"reason", reason}, {"message", "Nothing was reverted."}};
    if (!changed.is_null())
        out["changed_since"] = std::move(changed);
    return out;
}

json reverted(json not_restored)
{
    json out = {{"status", "reverted"}};
    if (!not_restored.empty())
        out["not_restored"] = std::move(not_restored);
    return out;
}

const ConfigOption *current(const Target &target, const JournalValue &value)
{
    return config_of(target, static_cast<Preset::Type>(value.tab)).option(value.key);
}

json revert_settings(const ChangeRecord &record)
{
    const Target target = parse_target(record.target);
    if (target_identity(target) != record.restore)
        return refused("The object or part at that number is not the one the agent changed; objects were added, removed or reordered since.");

    json changed = json::array();
    for (const JournalValue &value : record.values) {
        if (value.tab == Preset::TYPE_FILAMENT && target.scope == ApplyScope::Global)
            if (const auto reason = filament_slot_rejection(target))
                return refused("Filament settings: " + *reason);
        if (!value.after_known || !same(current(target, value), value.after.get()))
            changed.push_back(value.key);
    }
    if (!changed.empty())
        return refused("These settings have been changed since the agent changed them, likely by the user.", std::move(changed));

    std::map<Preset::Type, std::vector<SettingChange>> sets;
    std::vector<std::string>                           removals;
    for (const JournalValue &value : record.values) {
        if (value.before != nullptr)
            sets[static_cast<Preset::Type>(value.tab)].push_back({value.key, value.before});
        else
            removals.push_back(value.key);
    }
    {
        std::unique_ptr<Plater::TakeSnapshot> snapshot;
        if (target.scope != ApplyScope::Global)
            snapshot = std::make_unique<Plater::TakeSnapshot>(wxGetApp().plater(), "Agent revert " + record.change_id);
        for (const auto &[tab_type, changes] : sets)
            apply_to_target(target, tab_type, changes);
        remove_from_target(target, removals);
    }
    note_settings_changed();

    // Studio's handlers can change linked values again as the before-values go back in.
    json not_restored = json::array();
    for (const JournalValue &value : record.values)
        if (const ConfigOption *now = current(target, value); !same(now, value.before.get()))
            not_restored.push_back({{"key", value.key}, {"expected", value.before != nullptr ? option_to_json(*value.before) : json(nullptr)},
                                    {"now", now != nullptr ? option_to_json(*now) : json(nullptr)}});
    return reverted(std::move(not_restored));
}

json revert_filament_preset(const ChangeRecord &record)
{
    const size_t      slot   = record.target.at("slot").get<size_t>() - 1;
    const std::string before = record.restore.value("before", "");
    const std::string after  = record.restore.value("after", "");
    if (slot >= bundle().filament_presets.size())
        return refused("Slot " + std::to_string(slot + 1) + " no longer exists.");
    const std::string now = bundle().filament_presets[slot];
    if (now == before)
        return reverted(json::array());
    if (now != after)
        return refused("Slot " + std::to_string(slot + 1) + " now holds " + now + ", not what the agent picked.", json::array({{{"slot", slot + 1}}}));
    if (bundle().filaments.current_is_dirty() && bundle().filaments.get_edited_preset().name == now)
        return refused("Slot " + std::to_string(slot + 1) + "'s filament has unsaved edits made since.", json::array({{{"slot", slot + 1}}}));
    if (const auto reason = pick_filament_for_slot(slot, before))
        return refused(*reason);
    if (bundle().filament_presets[slot] != before)
        return reverted(json::array({{{"slot", slot + 1}, {"expected", before}, {"now", bundle().filament_presets[slot]}}}));
    return reverted(json::array());
}

json revert_process_preset(const ChangeRecord &record)
{
    PresetCollection &prints = bundle().prints;
    const std::string before = record.restore.value("before", "");
    const std::string after  = record.restore.value("after", "");
    const std::string now    = prints.get_selected_preset_name();
    if (now == before && unsaved_edits(prints) == record.restore["before_edits"])
        return reverted(json::array());
    if (now != after || unsaved_edits(prints) != record.restore["after_edits"])
        return refused("The process preset or its unsaved edits have changed since the agent switched it.", json::array({"process_preset"}));

    switch_process_preset(before, !record.restore["after_edits"].empty());
    json not_restored = json::array();
    for (int slot : restore_filament_slots(record.restore.value("before_slots", std::vector<std::string>())))
        not_restored.push_back({{"slot", slot}});
    if (prints.get_selected_preset_name() != before)
        not_restored.push_back({{"key", "process_preset"}, {"expected", before}, {"now", prints.get_selected_preset_name()}});
    else if (const json edits = unsaved_edits(prints); edits != record.restore["before_edits"])
        not_restored.push_back({{"key", "unsaved_edits"}, {"expected", record.restore["before_edits"]}, {"now", edits}});
    return reverted(std::move(not_restored));
}

json revert_object_filament(const ChangeRecord &record)
{
    Model     &model = wxGetApp().plater()->model();
    const auto index = record.target.at("object").get<size_t>() - 1;
    if (index >= model.objects.size() || model.objects[index]->id().id != record.restore.at("object_id").get<size_t>())
        return refused("Object " + std::to_string(index + 1) + " is not the one the agent changed; objects were added, removed or reordered since.");
    ModelObject &object = *model.objects[index];
    const json   now    = object_filament_state(object);
    if (now == record.restore["before"])
        return reverted(json::array());
    if (now != record.restore["after"])
        return refused("The object's filament has been changed since.", json::array({"extruder"}));
    restore_object_filament_state(object, record.restore["before"], "Agent revert " + record.change_id);
    return reverted(object_filament_state(object) == record.restore["before"] ? json::array() : json::array({"extruder"}));
}
} // namespace

json revert_change(const json &args)
{
    ChangeJournal    &journal   = change_journal();
    ChangeRecord     *newest    = journal.newest_active();
    const std::string requested = string_arg(args, "change_id");
    if (!requested.empty()) {
        const ChangeRecord *record = journal.find(requested);
        if (record == nullptr)
            throw BridgeError(404, "not_found", "No change " + requested + " in this project. The journal lasts one Studio session and is per project.");
        if (record->reverted)
            return {{"status", "already_reverted"}, {"change_id", requested}};
        if (record != newest)
            throw BridgeError(409, "revert_order", "Changes are reverted newest first. Revert " + newest->change_id + " first.");
    }
    if (newest == nullptr)
        return {{"status", "nothing_to_revert"}};

    const std::string change_id = newest->change_id;
    MutationScope     running("change_id", change_id);
    json              result;
    try {
        switch (newest->kind) {
        case ChangeKind::FilamentPreset: result = revert_filament_preset(*newest); break;
        case ChangeKind::ProcessPreset:  result = revert_process_preset(*newest); break;
        case ChangeKind::ObjectFilament: result = revert_object_filament(*newest); break;
        default:                         result = revert_settings(*newest); break;
        }
    } catch (const BridgeError &) {
        throw;
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "agent bridge: revert of " << change_id << " failed: " << e.what();
        throw BridgeError(500, "revert_failed", "Reverting " + change_id + " failed partway: " + e.what() + ". Check get_settings.");
    }
    result["change_id"] = change_id;
    // A dialog can outlast the call; get_studio_state's last_change then carries the outcome.
    if (ChangeRecord *record = journal.find(change_id)) {
        record->outcome["revert"] = result;
        if (result["status"] == "reverted")
            journal.mark_reverted(change_id);
    }
    return result;
}

}}} // namespace Slic3r::GUI::AgentBridge
