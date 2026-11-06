// Bridge tools that read presets and filaments. Runs on the UI thread.
#include <boost/algorithm/string.hpp>

#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include "AgentBridgeArgs.hpp"
#include "AgentBridgeReadHandlers.hpp"
#include "AgentBridgeSettingsJson.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
PresetBundle &bundle() { return *wxGetApp().preset_bundle; }

const char *preset_source(const Preset &preset)
{
    if (preset.is_project_embedded) return "project";
    if (preset.is_system)           return "system";
    if (preset.is_default)          return "default";
    return "user";
}

// What the Studio UI compares a preset against to mark values as changed. A system preset's parent
// is an internal template, so it is compared with its own saved copy, which leaves only project and
// unsaved edits. The saved copy needs real=true: plain find_preset returns the edited copy for the
// selected preset, and comparing that with itself would hide every change.
const Preset *comparison_base(PresetCollection &collection, const Preset &preset)
{
    const Preset *parent = preset.is_system ? nullptr : collection.get_preset_parent(preset);
    // The parent may itself be the selected preset, and get_preset_parent would return its edited copy.
    return collection.find_preset(parent != nullptr ? parent->name : preset.name, false, true);
}
} // namespace

json preset_section(PresetCollection &collection, const Preset &preset, const std::string &tab, SettingsFilter filter)
{
    // Preset configs can hold stray keys of another tab, with values that are not the ones in use.
    filter.own_tab_only = true;

    const Preset *base = comparison_base(collection, preset);
    json section = {{"preset", preset.name}, {"source", preset_source(preset)}};
    if (base != nullptr && base->name != preset.name)
        section["base_preset"] = base->name;
    section["settings"] = settings_to_json(preset.config, base != nullptr ? &base->config : nullptr, tab, filter);
    return section;
}

json list_presets(const json &args)
{
    const std::string kind            = string_arg(args, "kind");
    const bool        compatible_only = bool_arg(args, "compatible_only", true);

    PresetBundle           &presets = bundle();
    const PresetCollection *collection;
    if (kind == "process")
        collection = &presets.prints;
    else if (kind == "filament")
        collection = &presets.filaments;
    else if (kind == "printer")
        collection = &presets.printers;
    else
        throw BridgeError(400, "invalid_args", "kind must be process, filament or printer");

    const std::string selected = collection->get_selected_preset_name();
    json              list     = json::array();
    for (const Preset &preset : (*collection)()) {
        if (preset.is_default || (compatible_only && (!preset.is_visible || !preset.is_compatible)))
            continue;
        json item = {{"name", preset.name}, {"source", preset_source(preset)}};
        if (!preset.is_compatible)
            item["compatible"] = false;
        if (preset.name == selected)
            item["selected"] = true;
        list.push_back(std::move(item));
    }
    return {{"kind", kind}, {"count", list.size()}, {"presets", std::move(list)}};
}

json list_filaments(const json &args)
{
    const bool compatible_only = bool_arg(args, "compatible_only", true);

    PresetBundle &presets = bundle();
    json          list    = json::array();
    for (const Preset &preset : presets.filaments()) {
        if (preset.is_default || (compatible_only && (!preset.is_visible || !preset.is_compatible)))
            continue;
        json item = {{"name", preset.name}, {"source", preset_source(preset)}};
        item["type"]   = first_string_value(preset.config, "filament_type");
        item["vendor"] = first_string_value(preset.config, "filament_vendor");
        if (!preset.is_compatible)
            item["compatible"] = false;
        json slots = json::array();
        for (size_t i = 0; i < presets.filament_presets.size(); ++i)
            if (presets.filament_presets[i] == preset.name)
                slots.push_back(i + 1);
        if (!slots.empty())
            item["slots"] = std::move(slots);
        list.push_back(std::move(item));
    }
    return {{"count", list.size()}, {"filaments", std::move(list)}};
}

json get_filament(const json &args)
{
    PresetBundle &presets = bundle();

    std::string name = string_arg(args, "name");
    const int   slot = optional_index_arg(args, "slot", presets.filament_presets.size());
    if (slot >= 0) {
        if (!name.empty() && name != presets.filament_presets[slot])
            throw BridgeError(400, "invalid_args", "name and slot refer to different filaments; give one");
        name = presets.filament_presets[slot];
    }
    if (name.empty())
        throw BridgeError(400, "invalid_args", "Give a filament name or a slot number");

    const Preset *preset = presets.filaments.find_preset(name, false);
    if (preset == nullptr)
        throw BridgeError(404, "not_found", "No filament preset named: " + name);

    // One filament is a focused request, so unlike get_settings it returns all values by default.
    const SettingsFilter filter = SettingsFilter::parse(string_arg(args, "category", "filament"), bool_arg(args, "include_defaults", true));
    if (!filter.tab.empty() && !boost::iequals(filter.tab, "filament"))
        throw BridgeError(400, "invalid_args", "category must be within filament, e.g. filament/Cooling");

    json out = preset_section(presets.filaments, *preset, "filament", filter);
    out["compatible"] = preset->is_compatible;
    out["type"] = first_string_value(preset->config, "filament_type");
    return out;
}

}}} // namespace Slic3r::GUI::AgentBridge
