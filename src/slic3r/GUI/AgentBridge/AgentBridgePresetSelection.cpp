// Bridge tool that switches the process preset. UI thread.
#include "AgentBridgePresetSelection.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Tab.hpp"

#include "AgentBridgeApplyHandlers.hpp"
#include "AgentBridgeArgs.hpp"
#include "AgentBridgeChangeJournal.hpp"
#include "AgentBridgeFilamentHandlers.hpp"
#include "AgentBridgeMutationGate.hpp"
#include "AgentBridgeSliceJobs.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
PresetBundle &bundle() { return *wxGetApp().preset_bundle; }

// Studio's own setting for carrying edits over without its dialog, on for one switch only.
class AutoTransfer
{
public:
    explicit AutoTransfer(bool on) : m_on(on)
    {
        if (m_on) {
            m_previous = wxGetApp().app_config->get(KEY);
            wxGetApp().app_config->set(KEY, "true");
        }
    }
    ~AutoTransfer()
    {
        if (m_on)
            wxGetApp().app_config->set(KEY, m_previous);
    }
    AutoTransfer(const AutoTransfer &) = delete;
    AutoTransfer &operator=(const AutoTransfer &) = delete;

private:
    static constexpr const char *KEY = "auto_transfer_when_switch_preset";
    bool        m_on;
    std::string m_previous;
};

std::string unavailable_reason(const Preset &preset)
{
    if (!preset.is_compatible)
        return preset.name + " is not compatible with the printer " + bundle().printers.get_edited_preset().name;
    if (!preset.is_visible)
        return preset.name + " is hidden in Studio's process list";
    return std::string();
}

json slot_changes(const std::vector<std::string> &before, const std::vector<std::string> &after)
{
    json changes = json::array();
    for (size_t i = 0; i < std::min(before.size(), after.size()); ++i)
        if (before[i] != after[i])
            changes.push_back({{"slot", i + 1}, {"before", before[i]}, {"after", after[i]}});
    return changes;
}
} // namespace

json unsaved_edits(const PresetCollection &presets)
{
    json edits = json::object();
    const DynamicPrintConfig &config = presets.get_edited_preset().config;
    for (std::string key : presets.current_dirty_options()) {
        // Per-extruder variants are listed as key#index; the value is the whole list.
        key = key.substr(0, key.find('#'));
        if (config.has(key))
            edits[key] = config.opt_serialize(key);
    }
    return edits;
}

json unsaved_changes_rejection(const PresetCollection &presets, const std::string &what)
{
    json keys = json::array();
    for (const auto &item : unsaved_edits(presets).items())
        keys.push_back(item.key());
    return {{"status", "rejected"},
            {"code", "unsaved_changes"},
            {"preset", presets.get_edited_preset().name},
            {"unsaved_keys", std::move(keys)},
            {"message", what + " (" + presets.get_edited_preset().name + ") has unsaved edits, which switching would drop. Nothing was changed. Ask the user to save or discard them in Studio, or revert the agent's changes first."}};
}

void switch_process_preset(const std::string &name, bool transfer)
{
    AutoTransfer carry(transfer);
    // What the Process list does when the user picks an item.
    wxGetApp().get_tab(Preset::TYPE_PRINT)->select_preset(name);
    note_settings_changed();
}

std::vector<int> restore_filament_slots(const std::vector<std::string> &presets)
{
    std::vector<int> failed;
    for (size_t slot = 0; slot < presets.size() && slot < bundle().filament_presets.size(); ++slot)
        if (bundle().filament_presets[slot] != presets[slot])
            if (pick_filament_for_slot(slot, presets[slot]) || bundle().filament_presets[slot] != presets[slot])
                failed.push_back(static_cast<int>(slot + 1));
    return failed;
}

json select_process_preset(const json &args)
{
    const std::string name         = string_arg(args, "name");
    const bool        keep_changes = bool_arg(args, "keep_changes", false);
    if (name.empty())
        throw BridgeError(400, "invalid_args", "Give the preset name; list_presets with kind process lists them");

    PresetBundle     &presets = bundle();
    PresetCollection &prints  = presets.prints;
    const Preset     *preset  = prints.find_preset(name, false);
    if (preset == nullptr)
        throw BridgeError(404, "not_found", "No process preset named: " + name + ". list_presets lists them.");

    const std::string before = prints.get_selected_preset_name();
    json result = {{"before", before}};
    if (before == preset->name) {
        result["status"] = "already_selected";
        return result;
    }
    if (const std::string reason = unavailable_reason(*preset); !reason.empty())
        return {{"status", "rejected"}, {"reason", reason}, {"message", "Nothing was changed."}};
    const bool dirty = prints.current_is_dirty();
    if (dirty && !keep_changes)
        return unsaved_changes_rejection(prints, "The process preset");
    // Studio also asks about filament edits when the new process does not suit that filament, and
    // has no way to carry those over.
    if (presets.filaments.current_is_dirty() &&
        !is_compatible_with_print(presets.filaments.get_edited_preset_with_vendor_profile(), prints.get_preset_with_vendor_profile(*preset),
                                  presets.printers.get_edited_preset_with_vendor_profile()))
        return unsaved_changes_rejection(presets.filaments, "The filament on the Filament tab, which does not suit " + preset->name + ",");

    const std::vector<std::string> slots_before = presets.filament_presets;
    const std::string change_id = change_journal().begin({{"name", preset->name}}, {}, ChangeKind::ProcessPreset);
    change_journal().find(change_id)->restore = {{"before", before}, {"before_edits", unsaved_edits(prints)}, {"before_slots", slots_before}};
    result["change_id"]                     = change_id;
    MutationScope running("change_id", change_id);

    switch_process_preset(preset->name, dirty);

    const std::string after = prints.get_selected_preset_name();
    ChangeRecord     *record = change_journal().find(change_id);
    record->restore["after"]       = after;
    record->restore["after_edits"] = unsaved_edits(prints);
    result["after"]  = after;
    result["status"] = after == preset->name ? "applied" : "not_applied";
    if (after != preset->name)
        result["reason"] = "Studio kept another preset, possibly from the answer to a dialog";
    if (dirty)
        result["kept_changes"] = record->restore["after_edits"];
    if (json slots = slot_changes(slots_before, presets.filament_presets); !slots.empty())
        result["linked_changes"] = std::move(slots);
    change_journal().finish(change_id, result);
    return result;
}

}}} // namespace Slic3r::GUI::AgentBridge
