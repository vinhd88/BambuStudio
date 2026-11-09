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
#include "AgentBridgeGeometryChange.hpp"
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
    const Target target   = parse_target(record.target);
    const json   identity = target_identity(target, touched_tabs(record.values));
    if (identity.value("presets", json()) != record.restore.value("presets", json()))
        return refused("Another preset is selected than when the agent made this change, so its before-values belong to a different preset.",
                       json::array({"presets"}));
    if (identity != record.restore)
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
    // A retry after a revert that failed partway: the preset is back, but the slots may not be.
    // Only then: if the user put the preset back by hand, their slot picks are theirs.
    const bool retry       = record.outcome.contains("revert") && record.outcome["revert"].value("status", "") == "failed";
    const bool preset_back = now == before && unsaved_edits(prints) == record.restore["before_edits"];
    if (preset_back && !retry)
        return reverted(json::array());
    if (!preset_back && (now != after || unsaved_edits(prints) != record.restore["after_edits"]))
        return refused("The process preset or its unsaved edits have changed since the agent switched it.", json::array({"process_preset"}));

    if (!preset_back)
        switch_process_preset(before, !record.restore["after_edits"].empty());
    json not_restored = json::array();
    for (int slot : restore_filament_slots(record.restore.value("before_slots", std::vector<std::string>()),
                                           record.restore.value("after_slots", std::vector<std::string>())))
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

// Marks the newest change as done with, leaving Studio as it is, so older changes can be reverted.
// Only after its revert was refused or failed: an agent must not skip a change it could undo.
json skip_change(ChangeJournal &journal, ChangeRecord &newest)
{
    const std::string last_revert = newest.outcome.contains("revert") ? newest.outcome["revert"].value("status", "") : "";
    if (last_revert != "refused" && last_revert != "failed")
        throw BridgeError(409, "not_skippable", "Change " + newest.change_id + " can be reverted; try revert_change without skip first. Only a change whose revert was refused or failed can be skipped.");
    const json result = {{"status", "skipped"},
                         {"change_id", newest.change_id},
                         {"message", "Nothing was changed in Studio; " + newest.change_id + "'s values stay as they are. Older changes can be reverted now."}};
    newest.outcome["revert"] = result;
    journal.mark_reverted(newest.change_id);
    return result;
}

json revert_change(const json &args)
{
    ChangeJournal    &journal   = change_journal();
    ChangeRecord     *newest    = journal.newest_active();
    const std::string requested = string_arg(args, "change_id");
    const bool        skip      = bool_arg(args, "skip", false);
    if (!requested.empty()) {
        const ChangeRecord *record = journal.find(requested);
        if (record == nullptr)
            throw BridgeError(404, "not_found", "No change " + requested + " in this project. The journal lasts one Studio session and is per project.");
        if (record->reverted) {
            const bool skipped = record->outcome.contains("revert") && record->outcome["revert"].value("status", "") == "skipped";
            return {{"status", skipped ? "already_skipped" : "already_reverted"}, {"change_id", requested}};
        }
        if (record != newest)
            throw BridgeError(409, "revert_order", "Changes are reverted newest first. Revert " + newest->change_id + " first.");
    }
    if (newest == nullptr)
        return {{"status", "nothing_to_revert"}};
    if (skip) {
        if (requested.empty())
            throw BridgeError(400, "invalid_args", "skip needs change_id: the change to skip, which must be the newest not yet reverted.");
        return skip_change(journal, *newest);
    }

    const std::string change_id = newest->change_id;
    MutationScope     running("change_id", change_id);
    // Recorded so a failed revert can be skipped later.
    const auto note_failure = [&](const std::string &code, const std::string &message) {
        if (ChangeRecord *record = journal.find(change_id))
            record->outcome["revert"] = {{"status", "failed"}, {"code", code}, {"message", message}};
    };
    json result;
    try {
        switch (newest->kind) {
        case ChangeKind::FilamentPreset: result = revert_filament_preset(*newest); break;
        case ChangeKind::ProcessPreset:  result = revert_process_preset(*newest); break;
        case ChangeKind::ObjectFilament: result = revert_object_filament(*newest); break;
        case ChangeKind::Geometry:       result = revert_geometry_change(*newest); break;
        default:                         result = revert_settings(*newest); break;
        }
    } catch (const BridgeError &e) {
        // Busy or waiting answers go away on a retry; only real failures make a change skippable.
        if (e.status >= 500 && e.status != 503)
            note_failure(e.code, e.what());
        throw;
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "agent bridge: revert of " << change_id << " failed: " << e.what();
        const std::string message = "Reverting " + change_id + " failed partway: " + e.what() + ". Check get_settings. If it keeps failing, revert_change with skip moves past it.";
        note_failure("revert_failed", message);
        throw BridgeError(500, "revert_failed", message);
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
