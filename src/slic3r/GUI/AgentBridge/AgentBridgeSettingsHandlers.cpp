// Bridge tools that read settings, presets and filaments. Runs on the UI thread.
#include <boost/algorithm/string.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeReadHandlers.hpp"
#include "AgentBridgeSettingsJson.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
PresetBundle &bundle() { return *wxGetApp().preset_bundle; }

std::string string_arg(const json &args, const char *name, const std::string &fallback = std::string())
{
    const auto it = args.find(name);
    if (it == args.end() || it->is_null())
        return fallback;
    if (!it->is_string())
        throw BridgeError(400, "invalid_args", std::string(name) + " must be a string");
    return it->get<std::string>();
}

bool bool_arg(const json &args, const char *name, bool fallback)
{
    const auto it = args.find(name);
    if (it == args.end() || it->is_null())
        return fallback;
    if (!it->is_boolean())
        throw BridgeError(400, "invalid_args", std::string(name) + " must be true or false");
    return it->get<bool>();
}

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

bool filter_includes_tab(const SettingsFilter &filter, const char *tab)
{
    return filter.tab.empty() || boost::iequals(filter.tab, tab);
}

json global_settings(const SettingsFilter &filter)
{
    PresetBundle &presets = bundle();

    json out = {{"scope", "global"}};
    if (filter_includes_tab(filter, "process"))
        out["process"] = preset_section(presets.prints, presets.prints.get_edited_preset(), "process", filter);
    if (filter_includes_tab(filter, "printer"))
        out["printer"] = preset_section(presets.printers, presets.printers.get_edited_preset(), "printer", filter);
    if (filter_includes_tab(filter, "filament")) {
        json slots = json::array();
        for (size_t i = 0; i < presets.filament_presets.size(); ++i) {
            const Preset *preset = presets.filaments.find_preset(presets.filament_presets[i], false);
            if (preset == nullptr) {
                slots.push_back({{"slot", i + 1}, {"preset", presets.filament_presets[i]}, {"error", "preset not found"}});
                continue;
            }
            json slot    = preset_section(presets.filaments, *preset, "filament", filter);
            slot["slot"] = i + 1;
            slots.push_back(std::move(slot));
        }
        out["filaments"] = std::move(slots);
    }
    // Project-level values such as flush volumes live outside the presets, so they have no base to
    // compare with. They are only part of a full, unfiltered listing.
    if (filter.include_defaults && filter.tab.empty())
        out["project"] = settings_to_json(presets.project_config, nullptr, "process", filter);
    return out;
}

// Every key present was set at this scope, so all are returned, whatever include_defaults says. A key
// that differs from the global process preset comes as { value, base }, with base being that global
// value, not what an enclosing object would pass to a part. One equal to it comes as a bare value.
json scoped_settings(const ConfigBase &overrides, const SettingsFilter &filter)
{
    return settings_to_json(overrides, &bundle().prints.get_edited_preset().config, "process", SettingsFilter{filter.tab, filter.page, true, false});
}

ModelObject &required_object(const json &args)
{
    Model    &model = wxGetApp().plater()->model();
    const int index = optional_index_arg(args, "object", model.objects.size());
    if (index < 0)
        throw BridgeError(400, "invalid_args", "This scope needs an object number; get_project lists them");
    return *model.objects[index];
}
} // namespace

json get_settings(const json &args)
{
    const std::string    scope  = string_arg(args, "scope", "global");
    const SettingsFilter filter = SettingsFilter::parse(string_arg(args, "category"), bool_arg(args, "include_defaults", false));

    if (scope == "global")
        return global_settings(filter);

    if (scope == "plate") {
        PartPlateList &plates = wxGetApp().plater()->get_partplate_list();
        int            index  = optional_index_arg(args, "plate", plates.get_plate_count());
        if (index < 0)
            index = plates.get_curr_plate_index();
        return {{"scope", scope}, {"plate", index + 1}, {"settings", scoped_settings(*plates.get_plate(index)->config(), filter)}};
    }
    if (scope == "object") {
        const ModelObject &object = required_object(args);
        return {{"scope", scope}, {"object", args["object"]}, {"name", object.name}, {"settings", scoped_settings(object.config.get(), filter)}};
    }
    if (scope == "part") {
        const ModelObject &object = required_object(args);
        const int          index  = optional_index_arg(args, "part", object.volumes.size());
        if (index < 0)
            throw BridgeError(400, "invalid_args", "scope part needs a part number; get_project lists them");
        const ModelVolume &part = *object.volumes[index];
        return {{"scope", scope}, {"object", args["object"]}, {"part", index + 1}, {"name", part.name}, {"settings", scoped_settings(part.config.get(), filter)}};
    }
    if (scope == "height_range") {
        const ModelObject &object = required_object(args);
        const int          index  = optional_index_arg(args, "range", object.layer_config_ranges.size());
        if (index < 0)
            throw BridgeError(400, "invalid_args", "scope height_range needs a range number; get_project lists them");
        auto range = object.layer_config_ranges.begin();
        std::advance(range, index);
        return {{"scope", scope}, {"object", args["object"]}, {"range", index + 1}, {"min_z", range->first.first}, {"max_z", range->first.second},
                {"settings", scoped_settings(range->second.get(), filter)}};
    }
    throw BridgeError(400, "invalid_args", "scope must be global, plate, object, part or height_range");
}

json describe_settings(const json &args)
{
    std::vector<std::string> keys;
    if (const auto it = args.find("keys"); it != args.end() && !it->is_null()) {
        if (!it->is_array() || it->empty())
            throw BridgeError(400, "invalid_args", "keys must be a non-empty list of setting names");
        for (const json &key : *it) {
            if (!key.is_string())
                throw BridgeError(400, "invalid_args", "keys must be a list of setting names");
            keys.push_back(key.get<std::string>());
        }
    } else {
        const std::string category = string_arg(args, "category");
        if (category.empty())
            throw BridgeError(400, "invalid_args", "Give keys or a category such as process/Speed");
        keys = keys_in_category(SettingsFilter::parse(category, true));
    }

    json settings = json::array();
    json unknown  = json::array();
    for (const std::string &key : keys) {
        const ConfigOptionDef *def = print_config_def.get(key);
        // Hidden keys are reported as unknown, so a caller cannot tell whether they exist.
        if (def != nullptr && is_setting_exposed(key))
            settings.push_back(setting_definition_to_json(key, *def));
        else
            unknown.push_back(key);
    }
    json out = {{"settings", std::move(settings)}};
    if (!unknown.empty())
        out["unknown"] = std::move(unknown);
    return out;
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
    if (!filter_includes_tab(filter, "filament"))
        throw BridgeError(400, "invalid_args", "category must be within filament, e.g. filament/Cooling");

    json out = preset_section(presets.filaments, *preset, "filament", filter);
    out["compatible"] = preset->is_compatible;
    out["type"] = first_string_value(preset->config, "filament_type");
    return out;
}

}}} // namespace Slic3r::GUI::AgentBridge
