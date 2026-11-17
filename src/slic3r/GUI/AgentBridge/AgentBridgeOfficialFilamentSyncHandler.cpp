// Tool sync_filaments_from_official_studio and its revert. UI thread.
#include "AgentBridgeOfficialFilamentSyncHandler.hpp"

#include <algorithm>

#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include "AgentBridgeApplyHandlers.hpp"
#include "AgentBridgeArgs.hpp"
#include "AgentBridgeChangeJournal.hpp"
#include "AgentBridgeMutationGate.hpp"
#include "AgentBridgeOfficialFilamentSyncRun.hpp"
#include "AgentBridgePresetSelection.hpp"
#include "AgentBridgeTypes.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
json slots_json(const std::vector<FilamentSlot> &slots)
{
    json out = json::array();
    for (const FilamentSlot &slot : slots)
        out.push_back({{"preset", slot.preset}, {"colour", slot.colour}, {"multi_colour", slot.multi_colour}});
    return out;
}

std::vector<FilamentSlot> slots_from_json(const json &slots)
{
    std::vector<FilamentSlot> out;
    for (const json &slot : slots)
        out.push_back({slot.value("preset", ""), slot.value("colour", ""), slot.value("multi_colour", "")});
    return out;
}

bool same_slot(const FilamentSlot &a, const FilamentSlot &b) { return a.preset == b.preset && a.colour == b.colour; }

// The sync's refusals as the bridge reports errors: nothing was changed.
[[noreturn]] void refuse(const FilamentSyncRefused &e)
{
    const int status = e.code == "official_studio_not_found" ? 404 : 409;
    throw BridgeError(status, e.code, std::string(e.what()) + " Nothing was changed.");
}
} // namespace

json sync_filaments_from_official_studio(const json &args)
{
    const bool preview = bool_arg(args, "preview", true);
    // Always a preview first: every refusal comes before a change is recorded.
    FilamentSyncOutcome planned;
    try {
        planned = run_official_filament_sync(false);
    } catch (const FilamentSyncRefused &e) {
        refuse(e);
    }
    if (preview) {
        json result       = planned.report;
        result["status"]  = "preview";
        result["message"] = "Nothing was changed. Show the user the slots that change, and call again with preview false after their OK.";
        return result;
    }
    if (planned.report.contains("warning")) // unsaved filament edits, which the preset reload would drop
        return unsaved_changes_rejection(wxGetApp().preset_bundle->filaments, "The selected filament");

    const std::string change_id = change_journal().begin({{"tool", "sync_filaments_from_official_studio"}}, {}, ChangeKind::FilamentSync);
    change_journal().find(change_id)->restore = {{"before", slots_json(planned.before)}};
    MutationScope running("change_id", change_id);
    FilamentSyncOutcome done;
    try {
        done = run_official_filament_sync(true);
    } catch (const FilamentSyncRefused &e) {
        // Its checks run again before anything changes (the official Studio may have saved in
        // between): close the record as done, so it never blocks older reverts.
        change_journal().find(change_id)->restore["after"] = change_journal().find(change_id)->restore["before"];
        change_journal().finish(change_id, {{"status", "rejected"}, {"code", e.code}});
        change_journal().mark_reverted(change_id);
        refuse(e);
    } catch (const std::exception &e) {
        // What the slots hold now is what a revert must find.
        fail_change(change_id, {{"after", slots_json(current_filament_slots())}}, e);
    }
    change_journal().find(change_id)->restore["after"] = slots_json(done.after);
    // What a revert cannot put back, so it can say so.
    json &restore = change_journal().find(change_id)->restore;
    restore["updated_presets"] = done.report["presets"]["updated"];
    restore["slots_removed"]   = done.before.size() > done.after.size();
    json result = done.report;
    // Stopped part way (the user kept a slot Studio asked about) or a slot Studio did not take.
    bool partial = done.report.contains("stopped");
    for (const json &slot : done.report["slots"])
        partial |= slot["status"] == "not_applied";
    result["status"]    = partial ? "partial" : "applied";
    result["change_id"] = change_id;
    change_journal().finish(change_id, result);
    return result;
}

json revert_filament_sync(const ChangeRecord &record)
{
    const std::vector<FilamentSlot> before = slots_from_json(record.restore.value("before", json::array()));
    const std::vector<FilamentSlot> after  = slots_from_json(record.restore.value("after", json::array()));
    const std::vector<FilamentSlot> now    = current_filament_slots();
    const auto all_same = [](const std::vector<FilamentSlot> &a, const std::vector<FilamentSlot> &b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), same_slot);
    };
    if (all_same(now, before))
        return {{"status", "reverted"}};
    // A retry after a revert that stopped part way: the slot count no longer matches the sync's,
    // so the before-slots go back as they are.
    const std::string last_try = record.outcome.contains("revert") ? record.outcome["revert"].value("status", "") : "";
    const bool        retry    = last_try == "failed" || last_try == "partial";
    if (!retry && now.size() != after.size())
        return {{"status", "refused"},
                {"reason", "The number of filament slots has changed since the sync (" + std::to_string(after.size()) + " then, " +
                               std::to_string(now.size()) + " now)."},
                {"message", "Nothing was reverted."}};
    if (wxGetApp().preset_bundle->filaments.current_is_dirty())
        return {{"status", "refused"}, {"reason", "The filament " + wxGetApp().preset_bundle->filaments.get_edited_preset().name + " has unsaved edits made since."},
                {"message", "Nothing was reverted."}};

    // Slots the user changed since the sync are theirs: they stay as they are.
    std::vector<FilamentSlot> target  = before;
    json                      skipped = json::array();
    for (size_t i = 0; !retry && i < now.size(); ++i)
        if (!same_slot(now[i], after[i])) {
            if (i >= target.size())
                return {{"status", "refused"},
                        {"reason", "Slot " + std::to_string(i + 1) + ", which the sync added, has been changed since; reverting would remove it."},
                        {"message", "Nothing was reverted."}};
            target[i] = now[i];
            skipped.push_back({{"slot", i + 1}, {"reason", "changed since the sync, left as it is"}});
        }
    json not_restored = apply_filament_slots(target);
    for (json &slot : skipped)
        not_restored.push_back(std::move(slot));
    // A removal Studio asked about and the user declined: not done; revert_change again finishes it.
    bool stopped = false;
    for (const json &problem : not_restored)
        stopped |= problem["reason"].get<std::string>().rfind("stopped", 0) == 0;
    json result = {{"status", stopped ? "partial" : "reverted"},
                   {"presets_kept", "Custom filament presets the sync copied or updated stay in this Studio, with the official values."}};
    if (!record.restore.value("updated_presets", json::array()).empty())
        result["presets_not_restored"] = record.restore["updated_presets"];
    if (record.restore.value("slots_removed", false))
        result["objects_not_restored"] = "Objects that printed with a slot the sync removed were moved to another slot then; they stay there. Check get_project.";
    if (!not_restored.empty())
        result["not_restored"] = std::move(not_restored);
    return result;
}

}}} // namespace Slic3r::GUI::AgentBridge
