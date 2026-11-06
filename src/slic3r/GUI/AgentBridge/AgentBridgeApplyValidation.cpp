#include "AgentBridgeApplyValidation.hpp"

#include <algorithm>
#include <iterator>
#include <optional>

#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/DeviceCore/DevConfigUtil.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Tab.hpp"

#include "AgentBridgeArgs.hpp"
#include "AgentBridgeSettingValue.hpp"
#include "AgentBridgeSettingsTabMap.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
PresetBundle &bundle() { return *wxGetApp().preset_bundle; }


// The filament tab edits one preset at a time, and switching it can raise an unsaved-changes dialog.
std::optional<std::string> filament_slot_rejection(const Target &target)
{
    PresetBundle     &presets  = bundle();
    const std::string selected = presets.filaments.get_selected_preset_name();
    if (target.filament_slot < 0) {
        if (presets.filament_presets.size() > 1)
            return "this project has several filament slots; give slot. Only the one selected on the Filament tab (" + selected + ") can be changed";
        return std::nullopt;
    }
    if (presets.filament_presets[target.filament_slot] != selected)
        return "slot " + std::to_string(target.filament_slot + 1) + " is not selected on the Filament tab (" + selected + " is), so it cannot be changed yet";
    return std::nullopt;
}

SafetyLimits current_limits()
{
    PresetBundle             &presets  = bundle();
    const DynamicPrintConfig &filament = presets.filaments.get_edited_preset().config;
    const DynamicPrintConfig &printer  = presets.printers.get_edited_preset().config;

    const auto first_float = [&printer](const char *key) {
        const auto *option = printer.option<ConfigOptionFloats>(key);
        return option != nullptr && !option->values.empty() ? option->values.front() : 0.;
    };
    SafetyLimits limits;
    if (const auto *low = filament.option<ConfigOptionInts>("nozzle_temperature_range_low"); low != nullptr && !low->values.empty())
        limits.nozzle_temp_low = low->values.front();
    if (const auto *high = filament.option<ConfigOptionInts>("nozzle_temperature_range_high"); high != nullptr && !high->values.empty())
        limits.nozzle_temp_high = high->values.front();
    limits.bed_temp_max = DevPrinterConfigUtil::get_bed_temperature_limit(presets.printers.get_edited_preset().get_printer_type(&presets));
    limits.max_speed_xy = std::max(first_float("machine_max_speed_x"), first_float("machine_max_speed_y"));
    limits.max_speed_z  = first_float("machine_max_speed_z");
    limits.max_accel_xy = std::max(first_float("machine_max_acceleration_x"), first_float("machine_max_acceleration_y"));
    limits.max_jerk_xy  = std::max(first_float("machine_max_jerk_x"), first_float("machine_max_jerk_y"));
    return limits;
}

Preset::Type tab_of(const std::string &key)
{
    const SettingPlace *place = find_setting_place(key);
    return place != nullptr && std::string(place->tab) == "filament" ? Preset::TYPE_FILAMENT : Preset::TYPE_PRINT;
}


// What a key resolves to at this level now. A level that does not set it inherits the global value.
const ConfigOption *effective_value(const Target &target, const std::string &key)
{
    if (const ConfigOption *own = config_of(target, tab_of(key)).option(key))
        return own;
    if (const ConfigOption *global = bundle().prints.get_edited_preset().config.option(key))
        return global;
    const ConfigOptionDef *def = print_config_def.get(key);
    return def != nullptr ? def->default_value.get() : nullptr;
}
} // namespace

Target parse_target(const json &args)
{
    const std::string scope = string_arg(args, "scope", "global");
    Target            target;
    target.description = {{"scope", scope}};

    if (scope == "global") {
        target.filament_slot = optional_index_arg(args, "slot", bundle().filament_presets.size());
        return target;
    }
    if (scope == "plate") {
        PartPlateList &plates = wxGetApp().plater()->get_partplate_list();
        int            index  = optional_index_arg(args, "plate", plates.get_plate_count());
        if (index < 0)
            index = plates.get_curr_plate_index();
        target.scope                = ApplyScope::Plate;
        target.plate                = plates.get_plate(index);
        target.description["plate"] = index + 1;
        return target;
    }

    Model &model = wxGetApp().plater()->model();
    if (scope != "object" && scope != "part" && scope != "height_range")
        throw BridgeError(400, "invalid_args", "scope must be global, plate, object, part or height_range");
    const int object_index = optional_index_arg(args, "object", model.objects.size());
    if (object_index < 0)
        throw BridgeError(400, "invalid_args", "This scope needs an object number; get_project lists them");
    target.object                = model.objects[object_index];
    target.description["object"] = object_index + 1;

    if (scope == "object") {
        target.scope = ApplyScope::Object;
    } else if (scope == "part") {
        const int index = optional_index_arg(args, "part", target.object->volumes.size());
        if (index < 0)
            throw BridgeError(400, "invalid_args", "scope part needs a part number; get_project lists them");
        target.scope               = ApplyScope::Part;
        target.part                = target.object->volumes[index];
        target.description["part"] = index + 1;
    } else {
        const int index = optional_index_arg(args, "range", target.object->layer_config_ranges.size());
        if (index < 0)
            throw BridgeError(400, "invalid_args", "scope height_range needs a range number; get_project lists them");
        target.scope                = ApplyScope::HeightRange;
        target.range                = std::next(target.object->layer_config_ranges.begin(), index)->first;
        target.description["range"] = index + 1;
    }
    return target;
}

const DynamicPrintConfig &config_of(const Target &target, Preset::Type tab_type)
{
    switch (target.scope) {
    case ApplyScope::Plate:       return *target.plate->config();
    case ApplyScope::Object:      return target.object->config.get();
    case ApplyScope::Part:        return target.part->config.get();
    case ApplyScope::HeightRange: return target.object->layer_config_ranges.at(target.range).get();
    default:                      return *wxGetApp().get_tab(tab_type)->get_config();
    }
}

ValidatedRequest validate(const Target &target, const json &changes)
{
    const SafetyLimits limits = current_limits();
    ValidatedRequest   request;
    const auto         reject = [&request](const std::string &key, const std::string &reason) { request.rejections.push_back({{"key", key}, {"reason", reason}}); };

    for (const auto &[key, value] : changes.items()) {
        if (const auto reason = key_rejection(key, target.scope)) {
            reject(key, *reason);
            continue;
        }
        const Preset::Type tab_type = tab_of(key);
        if (target.scope == ApplyScope::Global && tab_type == Preset::TYPE_FILAMENT) {
            if (const auto reason = filament_slot_rejection(target)) {
                reject(key, *reason);
                continue;
            }
        }
        const ConfigOptionDef &def     = *print_config_def.get(key);
        const ConfigOption    *current = effective_value(target, key);
        if (current == nullptr) {
            reject(key, "has no current value to change");
            continue;
        }
        std::string error;
        std::shared_ptr<ConfigOption> parsed = parse_setting_value(def, *current, value, error);
        if (parsed == nullptr) {
            reject(key, error);
            continue;
        }
        if (const auto reason = value_rejection(key, def, *parsed, limits)) {
            reject(key, *reason);
            continue;
        }
        if (*parsed == *current && config_of(target, tab_type).has(key)) {
            request.unchanged.push_back(key);
            continue;
        }
        if (const auto warning = value_warning(key, *current, *parsed))
            request.warnings.push_back(*warning);
        request.changes_by_tab[tab_type].push_back({key, parsed});
    }
    return request;
}

}}} // namespace Slic3r::GUI::AgentBridge
