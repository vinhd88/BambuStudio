#include "AgentBridgeSafetyRules.hpp"

#include <algorithm>
#include <set>

#include <boost/algorithm/string.hpp>

#include "libslic3r/Preset.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "AgentBridgeSettingValue.hpp"
#include "AgentBridgeSettingsJson.hpp"
#include "AgentBridgeSettingsTabMap.hpp"

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
bool is_listed_read_only(const std::string &key)
{
    static const std::set<std::string> keys = {
        "printable_area", "printable_height", "filename_format",
        // These set the limits for nozzle temperature, so changing them would lift the limit.
        "nozzle_temperature_range_low", "nozzle_temperature_range_high",
    };
    return keys.count(key) > 0 || boost::ends_with(key, "_gcode") || boost::starts_with(key, "machine_max_");
}

bool in_keys(const std::vector<std::string> &keys, const std::string &key) { return std::find(keys.begin(), keys.end(), key) != keys.end(); }

// Mirrors what the Studio tab for each scope offers: TabPrintPlate, TabPrintObject, TabPrintPart, TabPrintLayer.
bool scope_has_key(ApplyScope scope, const std::string &key)
{
    static const std::vector<std::string> region_keys = PrintRegionConfig().keys();
    static const std::vector<std::string> object_keys = PrintObjectConfig().keys();

    // The other plate keys hold layer sequences that Studio edits in its own dialog.
    if (scope == ApplyScope::Plate)
        return key == "curr_bed_type" || key == "print_sequence" || key == "spiral_mode";
    if (!in_keys(Preset::print_options(), key))
        return false;
    switch (scope) {
    case ApplyScope::Object:      return in_keys(object_keys, key) || in_keys(region_keys, key);
    case ApplyScope::Part:        return in_keys(region_keys, key);
    case ApplyScope::HeightRange: return key == "layer_height" || in_keys(region_keys, key);
    default:                      return false;
    }
}

bool is_bed_temperature(const std::string &key)
{
    return boost::ends_with(key, "_plate_temp") || boost::ends_with(key, "_plate_temp_initial_layer");
}

std::string number_text(double value)
{
    std::string text = std::to_string(value);
    text.erase(text.find_last_not_of('0') + 1);
    if (!text.empty() && text.back() == '.')
        text.pop_back();
    return text;
}

// The first number above `limit`, with a reason naming what the limit is.
std::optional<std::string> above_limit(const ConfigOption &value, double limit, const std::string &what, const std::string &unit)
{
    if (limit <= 0)
        return std::nullopt;
    for (const NumericValue &number : numeric_values(value))
        if (!number.is_percent && number.value > limit)
            return number_text(number.value) + " " + unit + " is above the " + what + " of " + number_text(limit) + " " + unit;
    return std::nullopt;
}

bool any_lowered(const ConfigOption &before, const ConfigOption &after)
{
    const auto old_numbers = numeric_values(before);
    const auto new_numbers = numeric_values(after);
    for (size_t i = 0; i < std::min(old_numbers.size(), new_numbers.size()); ++i)
        if (new_numbers[i].value < old_numbers[i].value)
            return true;
    return false;
}
} // namespace

std::optional<std::string> key_rejection(const std::string &key, ApplyScope scope)
{
    if (print_config_def.get(key) == nullptr || !is_user_setting(key))
        return "unknown setting";
    if (is_listed_read_only(key))
        return "read-only: agents cannot change G-code, printer limits or output naming";

    if (scope != ApplyScope::Global) {
        if (!scope_has_key(scope, key))
            return "cannot be set at this level";
        return std::nullopt;
    }
    const SettingPlace *place = find_setting_place(key);
    if (place == nullptr)
        return "read-only: not on the process or filament tab";
    if (std::string(place->tab) == "printer")
        return "read-only: agents cannot change printer settings";
    // The tab map also places a few plate keys, such as curr_bed_type, on the process tab. They are
    // not part of the preset, and writing them there would change nothing but the preset.
    const auto &preset_keys = std::string(place->tab) == "filament" ? Preset::filament_options() : Preset::print_options();
    if (!in_keys(preset_keys, key))
        return "cannot be set globally; set it per plate with scope plate";
    return std::nullopt;
}

std::optional<std::string> value_rejection(const std::string &key, const ConfigOptionDef &def, const ConfigOption &value, const SafetyLimits &limits)
{
    if (key == "nozzle_temperature" || key == "nozzle_temperature_initial_layer") {
        for (const NumericValue &number : numeric_values(value)) {
            if (limits.nozzle_temp_low && number.value < *limits.nozzle_temp_low)
                return number_text(number.value) + " C is below this filament's range, which starts at " + std::to_string(*limits.nozzle_temp_low) + " C";
            if (limits.nozzle_temp_high && number.value > *limits.nozzle_temp_high)
                return number_text(number.value) + " C is above this filament's range, which ends at " + std::to_string(*limits.nozzle_temp_high) + " C";
        }
        return std::nullopt;
    }
    if (is_bed_temperature(key))
        return above_limit(value, limits.bed_temp_max, "printer's bed limit", "C");
    if (boost::ends_with(key, "_jerk"))
        return above_limit(value, limits.max_jerk_xy, "printer's jerk limit", "mm/s");
    if (key == "travel_speed_z")
        return above_limit(value, limits.max_speed_z, "printer's Z speed limit", "mm/s");
    // Some units read "mm/s or %": a plain number there is still a speed. Acceleration first, since
    // its unit also starts with mm/s.
    if (boost::starts_with(def.sidetext, "mm/s\xC2\xB2"))
        return above_limit(value, limits.max_accel_xy, "printer's acceleration limit", "mm/s2");
    if (boost::starts_with(def.sidetext, "mm/s"))
        return above_limit(value, limits.max_speed_xy, "printer's speed limit", "mm/s");
    return std::nullopt;
}

std::optional<std::string> value_warning(const std::string &key, const ConfigOption &before, const ConfigOption &after)
{
    static const std::set<std::string> cooling_keys = {"slow_down_layer_time", "fan_min_speed", "fan_max_speed", "additional_cooling_fan_speed", "fan_cooling_layer_time"};
    if (cooling_keys.count(key) > 0 && any_lowered(before, after))
        return key + " was lowered. Slice time estimates do not show the quality cost of less cooling; overhangs and small layers can suffer.";
    return std::nullopt;
}

}}} // namespace Slic3r::GUI::AgentBridge
