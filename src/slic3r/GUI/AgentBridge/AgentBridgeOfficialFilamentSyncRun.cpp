// Sync filaments from the official Studio into the open project: preview, or copy + reload + apply.
#include "AgentBridgeOfficialFilamentSyncRun.hpp"

#include <algorithm>

#include <boost/filesystem.hpp>

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeAgentDataFolder.hpp"
#include "AgentBridgeMutationGate.hpp"
#include "AgentBridgeOfficialFilamentSync.hpp"

namespace fs = boost::filesystem;
using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
PresetBundle &bundle() { return *wxGetApp().preset_bundle; }

bool contains(const std::vector<std::string> &names, const std::string &name) { return std::find(names.begin(), names.end(), name) != names.end(); }

OfficialFilamentSlots read_official(const fs::path &official, const fs::path &agent)
{
    boost::system::error_code ec;
    if (official.empty() || !fs::is_directory(official, ec))
        throw FilamentSyncRefused("official_studio_not_found", "The official Bambu Studio's data folder was not found, so there is nothing to sync from.");
    if (fs::equivalent(official, agent, ec))
        throw FilamentSyncRefused("official_studio_not_found", "This Studio uses the official Bambu Studio's own data folder; there is nothing to sync.");
    try {
        return read_official_filament_slots(official);
    } catch (const std::exception &e) {
        throw FilamentSyncRefused("official_config_unreadable", e.what());
    }
}

json presets_json(const FilamentPresetCopy &copy)
{
    json presets = {{"added", copy.added}, {"updated", copy.updated}, {"unchanged", copy.unchanged.size()}};
    if (!copy.backup.empty())
        presets["backup"] = copy.backup;
    if (!copy.problems.empty())
        presets["problems"] = copy.problems;
    return presets;
}
} // namespace

FilamentSyncOutcome run_official_filament_sync(bool apply)
{
    // Signed in, Studio keeps presets in the account's folder, not user/default: the copy would land
    // where it never looks, and reloading would switch its save folder.
    if (!wxGetApp().app_config->get("preset_folder").empty())
        throw FilamentSyncRefused("signed_in", "This Studio is signed in to a Bambu account, so it keeps its own filament presets; sync is only for "
                                                "the signed-out agent build.");
    if (wxGetApp().plater()->sidebar().is_new_project_in_gcode3mf())
        throw FilamentSyncRefused("gcode_project", "The open project is a sliced G-code file; its filaments cannot be changed.");
    if (apply && !running_mutation_id().empty())
        throw FilamentSyncRefused("studio_busy", "An agent change is still running in Studio. Try again when it has finished.");
    const fs::path              official_dir = official_data_dir_from_home();
    const fs::path              agent_dir    = data_dir();
    const OfficialFilamentSlots official     = read_official(official_dir, agent_dir);

    PresetCollection &filaments = bundle().filaments;
    const bool        unsaved   = filaments.current_is_dirty();
    if (apply && unsaved)
        throw FilamentSyncRefused("unsaved_filament_edits", "The filament " + filaments.get_edited_preset().name +
                                                                " has unsaved changes. Save or discard them first, then sync again.");

    const FilamentPresetCopy copy = copy_official_filament_presets(official_dir, agent_dir, !apply);
    if (apply && (!copy.added.empty() || !copy.updated.empty()))
        reload_user_filament_presets(copy.updated);

    FilamentSyncOutcome outcome;
    outcome.before = current_filament_slots();

    // What each slot should become. A preset this Studio cannot use leaves the slot's own preset
    // (or, for a new slot, the last slot's), with the official colour.
    std::vector<std::string> skipped;
    int                      mixed = 0;
    for (size_t i = 0; i < official.presets.size(); ++i) {
        if (official.mixed[i]) {
            ++mixed;
            continue;
        }
        FilamentSlot      want{official.presets[i], official.colours[i], official.multi_colours[i]};
        std::string       reason = unusable_reason(want.preset);
        const size_t      index  = outcome.after.size();
        // In a preview the copied presets are not loaded yet.
        if (!apply && contains(copy.added, want.preset) && bundle().filaments.find_preset(want.preset, false) == nullptr)
            reason.clear();
        if (!reason.empty() && !outcome.before.empty())
            want.preset = outcome.before[std::min(index, outcome.before.size() - 1)].preset;
        outcome.after.push_back(want);
        skipped.push_back(reason);
    }

    if (outcome.after.empty())
        throw FilamentSyncRefused("official_config_unreadable", "The official Bambu Studio has no filament slots this Studio can use (only mixed-filament ones).");

    json problems = json::array();
    if (apply) {
        problems      = apply_filament_slots(outcome.after);
        outcome.after = current_filament_slots();
    }

    json slots = json::array();
    for (size_t i = 0; i < std::max(outcome.before.size(), skipped.size()); ++i) {
        json slot = {{"slot", i + 1}};
        if (i < outcome.before.size())
            slot["before"] = slot_json(outcome.before[i]);
        if (i < outcome.after.size())
            slot["after"] = slot_json(outcome.after[i]);
        const bool same = i < outcome.before.size() && i < outcome.after.size() && outcome.before[i].preset == outcome.after[i].preset &&
                          outcome.before[i].colour == outcome.after[i].colour;
        // Beyond the official count a slot is removed, or kept when Studio (or the user) kept it.
        slot["status"] = i >= skipped.size() ? (apply && i < outcome.after.size() ? "kept" : "removed")
                         : !skipped[i].empty() ? "skipped"
                         : same                ? "same"
                                               : "changed";
        if (i < skipped.size() && !skipped[i].empty())
            slot["reason"] = skipped[i];
        for (const json &problem : problems)
            if (problem["slot"] == i + 1) {
                slot["status"] = "not_applied";
                slot["reason"] = problem["reason"];
            }
        slots.push_back(slot);
    }

    const std::string current_printer = bundle().printers.get_selected_preset_name();
    outcome.report = {{"official_printer", official.printer},
                      {"current_printer", current_printer},
                      {"printer_matches", official.printer == current_printer},
                      {"presets", presets_json(copy)},
                      {"slot_count", {{"before", outcome.before.size()}, {"after", apply ? outcome.after.size() : skipped.size()}}},
                      {"slots", slots},
                      {"mixed_slots_left_out", mixed}};
    if (!apply && unsaved)
        outcome.report["warning"] = "The filament " + filaments.get_edited_preset().name + " has unsaved changes; save or discard them before syncing.";
    for (const json &problem : problems)
        if (problem["reason"].get<std::string>().rfind("stopped", 0) == 0)
            outcome.report["stopped"] = problem["reason"];
    return outcome;
}

}}} // namespace Slic3r::GUI::AgentBridge
