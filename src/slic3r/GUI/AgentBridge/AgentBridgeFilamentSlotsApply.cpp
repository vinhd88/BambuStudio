// Sets the project's filament slots through the sidebar's own actions. UI thread.
#include "AgentBridgeFilamentSlotsApply.hpp"

#include <algorithm>
#include <exception>
#include <regex>

#include <wx/colour.h>

#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/PresetComboBoxes.hpp"
#include "slic3r/GUI/Tab.hpp"

#include "AgentBridgeFilamentHandlers.hpp"
#include "AgentBridgeSliceJobs.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
PresetBundle &bundle() { return *wxGetApp().preset_bundle; }
Sidebar      &sidebar() { return wxGetApp().plater()->sidebar(); }
size_t        slot_count() { return sidebar().combos_filament().size(); }

bool valid_colour(const std::string &colour)
{
    static const std::regex hex("#[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?");
    return std::regex_match(colour, hex);
}

std::string string_at(const char *key, size_t index)
{
    const auto *option = bundle().project_config.option<ConfigOptionStrings>(key);
    return option != nullptr && index < option->values.size() ? option->values[index] : std::string();
}

// As the colour picker sets a slot's colour (PresetComboBox::update_ams_color), for several slots at
// once, then the same per-slot event, which recalculates flushing volumes.
void set_colours(const std::vector<std::pair<size_t, FilamentSlot>> &changes)
{
    if (changes.empty())
        return;
    DynamicPrintConfig &config = bundle().project_config;
    auto *head = static_cast<ConfigOptionStrings *>(config.option("filament_colour")->clone());
    auto *pack = static_cast<ConfigOptionStrings *>(config.option("filament_multi_colour")->clone());
    for (const auto &[index, slot] : changes) {
        if (index >= head->values.size())
            continue;
        head->values[index] = slot.colour;
        if (index < pack->values.size())
            pack->values[index] = slot.multi_colour.empty() ? slot.colour : slot.multi_colour;
    }
    DynamicPrintConfig changed;
    changed.set_key_value("filament_colour", head);
    changed.set_key_value("filament_multi_colour", pack);
    config.apply(changed);
    wxGetApp().plater()->on_config_change(changed);
    for (const auto &[index, slot] : changes) {
        auto *event = new wxCommandEvent(EVT_FILAMENT_COLOR_CHANGED);
        event->SetInt(static_cast<int>(index));
        wxQueueEvent(wxGetApp().plater(), event);
        if (index < slot_count())
            sidebar().combos_filament()[index]->update();
    }
}
} // namespace

std::vector<FilamentSlot> current_filament_slots()
{
    std::vector<FilamentSlot> slots;
    const auto &presets = bundle().filament_presets;
    for (size_t i = 0; i < slot_count() && i < presets.size(); ++i)
        slots.push_back({presets[i], string_at("filament_colour", i), string_at("filament_multi_colour", i)});
    return slots;
}

json slot_json(const FilamentSlot &slot) { return {{"preset", slot.preset}, {"colour", slot.colour}}; }

std::string unusable_reason(const std::string &name)
{
    const Preset *preset = bundle().filaments.find_preset(name, false);
    if (preset == nullptr)
        return "no filament preset named " + name + " in this Studio";
    if (!preset->is_compatible)
        return name + " does not fit the printer " + bundle().printers.get_edited_preset().name;
    if (!preset->is_visible)
        return name + " is hidden in Studio's filament list";
    return "";
}

json apply_filament_slots(const std::vector<FilamentSlot> &target)
{
    json problems = json::array();
    // 1. Remove from the end. Studio asks first when objects print with the slot; "No" stops here.
    while (slot_count() > target.size() && slot_count() > 1) {
        const size_t before = slot_count();
        sidebar().delete_filament(before - 1);
        if (slot_count() == before) {
            problems.push_back({{"slot", before}, {"reason", "stopped: Studio kept this slot (in use, or the removal was declined)"}});
            break;
        }
    }
    // 2. Add at the end, with the preset and colour at once when Studio offers the preset.
    while (slot_count() < target.size()) {
        const size_t       index = slot_count();
        const FilamentSlot &slot = target[index];
        std::string        colour = valid_colour(slot.colour) ? slot.colour : string_at("filament_colour", 0);
        if (!valid_colour(colour))
            colour = "#FFFFFF";
        sidebar().add_custom_filament(wxColour(colour), unusable_reason(slot.preset).empty() ? slot.preset : std::string());
        if (slot_count() == index) {
            problems.push_back({{"slot", index + 1}, {"reason", "stopped: Studio did not add another slot (it may be at its limit)"}});
            break;
        }
    }
    // 3. Presets, through each slot's list, as a click there would.
    const size_t count = std::min(slot_count(), target.size());
    for (size_t i = 0; i < count; ++i) {
        if (bundle().filament_presets[i] == target[i].preset)
            continue;
        std::string reason = unusable_reason(target[i].preset);
        if (reason.empty())
            if (const auto declined = pick_filament_for_slot(i, target[i].preset))
                reason = *declined;
        if (reason.empty() && bundle().filament_presets[i] != target[i].preset)
            reason = "Studio kept another filament, possibly from the answer to a dialog";
        if (!reason.empty())
            problems.push_back({{"slot", i + 1}, {"reason", reason}});
    }
    // 4. Colours last: picking a preset can reset a slot's colour.
    std::vector<std::pair<size_t, FilamentSlot>> colours;
    for (size_t i = 0; i < count; ++i)
        if (valid_colour(target[i].colour) &&
            (string_at("filament_colour", i) != target[i].colour ||
             (!target[i].multi_colour.empty() && string_at("filament_multi_colour", i) != target[i].multi_colour)))
            colours.emplace_back(i, target[i]);
    set_colours(colours);
    note_settings_changed();
    return problems;
}

void reload_user_filament_presets(const std::vector<std::string> &updated)
{
    PresetCollection &filaments = bundle().filaments;
    const std::string selected  = filaments.get_selected_preset_name();
    // Presets already loaded are skipped by the loader, so changed ones are dropped first. Only user
    // presets: a system preset of the same name is never touched.
    for (const std::string &name : updated) {
        const auto it = std::find_if(filaments.begin(), filaments.end(), [&name](const Preset &p) { return p.name == name && p.is_user(); });
        if (it != filaments.end())
            filaments.erase(it);
    }
    // Erasing shifts the selection's index: select by name again (the first visible preset if the
    // selected one was dropped), so the loader re-selects the right one.
    filaments.select_preset_by_name(selected, true);
    std::exception_ptr failed;
    try {
        bundle().load_user_presets(DEFAULT_USER_FOLDER_NAME, ForwardCompatibilitySubstitutionRule::Enable);
    } catch (...) {
        failed = std::current_exception();
    }
    // Whatever the loader did: drop pointers to erased presets (the loader does this only when it
    // succeeds), never leave a slot naming a preset that is gone, and show the result.
    bundle().set_calibrate_printer("");
    filaments.select_preset_by_name(selected, true);
    bool slot_changed = false;
    for (std::string &name : bundle().filament_presets)
        if (std::find(updated.begin(), updated.end(), name) != updated.end()) {
            slot_changed = true;
            if (filaments.find_preset(name, false) == nullptr)
                name = filaments.get_selected_preset_name();
        }
    bundle().update_multi_material_filament_presets();
    if (Tab *tab = wxGetApp().get_tab(Preset::TYPE_FILAMENT))
        tab->load_current_preset(); // its fields show the reloaded values
    wxGetApp().mainframe->update_side_preset_ui();
    // A slot's preset has new values under the same name: the slice result is out of date.
    if (slot_changed) {
        wxGetApp().plater()->on_config_change(bundle().full_config());
        note_settings_changed();
    }
    if (failed)
        std::rethrow_exception(failed);
}

}}} // namespace Slic3r::GUI::AgentBridge
