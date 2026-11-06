// Bridge tools that change which filament prints what: the preset in a project slot, and the slot
// an object or part uses. UI thread.
#include "AgentBridgeFilamentHandlers.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/PresetComboBoxes.hpp"

#include "AgentBridgeApplyHandlers.hpp"
#include "AgentBridgeArgs.hpp"
#include "AgentBridgeChangeJournal.hpp"
#include "AgentBridgeMutationGate.hpp"
#include "AgentBridgePresetSelection.hpp"
#include "AgentBridgeSliceJobs.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
PresetBundle &bundle() { return *wxGetApp().preset_bundle; }

PlaterPresetComboBox &slot_combo(size_t slot)
{
    auto &combos = wxGetApp().plater()->sidebar().combos_filament();
    if (slot >= combos.size() || combos[slot] == nullptr)
        throw BridgeError(503, "studio_busy", "The sidebar has no filament list for slot " + std::to_string(slot + 1) + " yet. Try again shortly.");
    return *combos[slot];
}

// The row a user would click for this preset. Labels, disabled rows and AMS trays carry client data;
// preset rows do not. Rows show aliases, which Studio resolves to a preset name the same way here.
std::optional<int> combo_row(PlaterPresetComboBox &combo, const std::string &name)
{
    for (unsigned int row = 0; row < combo.GetCount(); ++row) {
        if (combo.GetClientData(row) != nullptr)
            continue;
        const std::string label = Preset::remove_suffix_modified(combo.GetString(row).ToUTF8().data());
        if (bundle().get_preset_name_by_alias(Preset::TYPE_FILAMENT, label) == name)
            return static_cast<int>(row);
    }
    return std::nullopt;
}

std::string not_offered_reason(const Preset &preset, size_t slot)
{
    if (!preset.is_compatible)
        return preset.name + " is not compatible with the printer " + bundle().printers.get_edited_preset().name;
    if (!preset.is_visible)
        return preset.name + " is hidden in Studio's filament list; the user can show it there first";
    return preset.name + " is not offered in slot " + std::to_string(slot + 1) + "'s list";
}

void refresh_after_object_filament_change()
{
    Plater &plater = *wxGetApp().plater();
    plater.get_partplate_list().reset_thumbnail_assembly_view_data();
    wxGetApp().obj_list()->sync_filament_from_model();
    plater.update();
    plater.sidebar().update_incompatible_nozzle_notifications();
    wxGetApp().obj_list()->Refresh();
}

void set_extruder(ModelConfig &config, const json &extruder)
{
    if (extruder.is_null())
        config.erase("extruder");
    else
        config.set_key_value("extruder", new ConfigOptionInt(extruder.get<int>()));
}
} // namespace

std::optional<std::string> pick_filament_for_slot(size_t slot, const std::string &name)
{
    const Preset *preset = bundle().filaments.find_preset(name, false);
    if (preset == nullptr)
        return "no filament preset named " + name;
    PlaterPresetComboBox &combo = slot_combo(slot);
    const auto            row   = combo_row(combo, preset->name);
    if (!row)
        return not_offered_reason(*preset, slot);
    // The same notification a click sends, so Studio's own handler does the rest.
    combo.SelectAndNotify(*row);
    note_settings_changed();
    return std::nullopt;
}

json select_filament(const json &args)
{
    PresetBundle &presets = bundle();
    const int     slot    = optional_index_arg(args, "slot", presets.filament_presets.size());
    const std::string name = string_arg(args, "name");
    if (slot < 0 || name.empty())
        throw BridgeError(400, "invalid_args", "Give slot and the preset name; list_filaments lists them");
    const Preset *preset = presets.filaments.find_preset(name, false);
    if (preset == nullptr)
        throw BridgeError(404, "not_found", "No filament preset named: " + name + ". list_filaments lists them.");

    const std::string before = presets.filament_presets[slot];
    json result = {{"slot", slot + 1}, {"before", before}};
    if (before == preset->name) {
        result["status"] = "already_selected";
        return result;
    }
    // Studio keeps one edited copy of a filament, the one on the Filament tab, and never carries its
    // edits to another preset. Dropping them is the user's call.
    if (presets.filaments.current_is_dirty() && presets.filaments.get_edited_preset().name == before)
        return unsaved_changes_rejection(presets.filaments, "Slot " + std::to_string(slot + 1) + "'s filament");
    PlaterPresetComboBox &combo = slot_combo(slot);
    const auto            row   = combo_row(combo, preset->name);
    if (!row)
        return {{"status", "rejected"}, {"reason", not_offered_reason(*preset, slot)}, {"message", "Nothing was changed."}};

    const std::string change_id = change_journal().begin({{"slot", slot + 1}}, {}, ChangeKind::FilamentPreset);
    change_journal().find(change_id)->restore = {{"before", before}};
    result["change_id"]                     = change_id;
    MutationScope running("change_id", change_id);

    try {
        combo.SelectAndNotify(*row);
    } catch (const std::exception &e) {
        // What the slot holds now is what a revert must find, or the record blocks every older one.
        fail_change(change_id, {{"after", presets.filament_presets[slot]}}, e);
    }
    note_settings_changed();

    const std::string after = presets.filament_presets[slot];
    change_journal().find(change_id)->restore["after"] = after;
    result["after"]  = after;
    result["status"] = after == preset->name ? "applied" : "not_applied";
    if (after != preset->name)
        result["reason"] = "Studio kept another filament, possibly from the answer to a dialog";
    change_journal().finish(change_id, result);
    return result;
}

json object_filament_state(const ModelObject &object)
{
    const auto entry = [](int volume, const ModelConfig &config) {
        return json{{"volume", volume}, {"extruder", config.has("extruder") ? json(config.opt_int("extruder")) : json(nullptr)}};
    };
    json state = json::array({entry(-1, object.config)});
    for (size_t i = 0; i < object.volumes.size(); ++i)
        state.push_back(entry(static_cast<int>(i), object.volumes[i]->config));
    return state;
}

void restore_object_filament_state(ModelObject &object, const json &state, const std::string &snapshot_name)
{
    {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), snapshot_name);
        for (const json &entry : state) {
            const int volume = entry.at("volume").get<int>();
            if (volume < static_cast<int>(object.volumes.size()))
                set_extruder(volume < 0 ? object.config : object.volumes[volume]->config, entry.at("extruder"));
        }
    }
    refresh_after_object_filament_change();
}

json set_object_filament(const json &args)
{
    Model     &model        = wxGetApp().plater()->model();
    const int  object_index = optional_index_arg(args, "object", model.objects.size());
    const int  slot         = optional_index_arg(args, "slot", bundle().filament_presets.size());
    if (object_index < 0 || slot < 0)
        throw BridgeError(400, "invalid_args", "Give object and slot; get_project lists them");
    ModelObject &object     = *model.objects[object_index];
    const int    part_index = optional_index_arg(args, "part", object.volumes.size());
    json         target     = {{"object", object_index + 1}};
    if (part_index >= 0) {
        const ModelVolume &part = *object.volumes[part_index];
        if (object.volumes.size() == 1)
            throw BridgeError(400, "invalid_args", "This object has one part; set the object's filament instead");
        if (!part.is_model_part() && !part.is_modifier())
            throw BridgeError(400, "invalid_args", "Only parts and modifiers print with a filament of their own");
        target["part"] = part_index + 1;
    }

    // Writes what Studio's filament column does: the object's setting, or the part's. Setting the
    // object makes all its parts follow it again.
    const json before = object_filament_state(object);
    json       after  = before;
    for (json &entry : after) {
        const int volume = entry["volume"].get<int>();
        if (volume == part_index)
            entry["extruder"] = slot + 1;
        else if (part_index < 0 && object.volumes[volume]->is_model_part())
            entry["extruder"] = nullptr;
    }
    json result = target;
    result["filament"] = slot + 1;
    if (after == before) {
        result["status"] = "already_set";
        return result;
    }

    const std::string change_id = change_journal().begin(target, {}, ChangeKind::ObjectFilament);
    change_journal().find(change_id)->restore = {{"object_id", object.id().id}, {"before", before}, {"after", after}};
    result["change_id"]                     = change_id;
    MutationScope running("change_id", change_id);
    try {
        restore_object_filament_state(object, after, "Agent filament change " + change_id);
    } catch (const std::exception &e) {
        fail_change(change_id, {{"after", object_filament_state(object)}}, e);
    }

    result["status"] = "applied";
    change_journal().finish(change_id, result);
    return result;
}

}}} // namespace Slic3r::GUI::AgentBridge
