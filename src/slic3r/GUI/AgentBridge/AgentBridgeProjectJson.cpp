#include "AgentBridgeProjectJson.hpp"

#include <cmath>

#include "libslic3r/ExtrusionEntity.hpp"

#include "AgentBridgeSettingsJson.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
json override_keys(const ModelConfig &config)
{
    json keys = json::array();
    for (const std::string &key : config.keys())
        if (is_user_setting(key))
            keys.push_back(key);
    return keys;
}

const char *volume_type_name(const ModelVolume &volume)
{
    if (volume.is_model_part())        return "part";
    if (volume.is_negative_volume())   return "negative";
    if (volume.is_modifier())          return "modifier";
    if (volume.is_support_enforcer())  return "support_enforcer";
    if (volume.is_support_blocker())   return "support_blocker";
    return "other";
}

// ConfigOption "extruder" is 1-based with 0 meaning inherit; an unset part prints with slot 1.
int filament_slot(const ModelVolume &volume)
{
    const int extruder = volume.extruder_id();
    return extruder <= 0 ? 1 : extruder;
}

double rounded(double value, double scale = 100.0) { return std::round(value * scale) / scale; }

json part_to_json(const ModelVolume &volume, size_t index)
{
    json part = {{"part", index + 1}, {"name", volume.name}, {"type", volume_type_name(volume)}};
    if (volume.is_model_part())
        part["filament"] = filament_slot(volume);
    json overrides = override_keys(volume.config);
    if (!overrides.empty())
        part["overrides"] = std::move(overrides);
    return part;
}

json object_to_json(const ModelObject &object, size_t index)
{
    json out = {{"object", index + 1}, {"name", object.name}, {"instances", object.instances.size()}, {"printable", object.printable}};
    // Parts without a filament of their own follow this one.
    out["filament"] = object.config.has("extruder") && object.config.opt_int("extruder") > 0 ? object.config.opt_int("extruder") : 1;

    json overrides = override_keys(object.config);
    if (!overrides.empty())
        out["overrides"] = std::move(overrides);

    json parts = json::array();
    for (size_t i = 0; i < object.volumes.size(); ++i)
        parts.push_back(part_to_json(*object.volumes[i], i));
    out["parts"] = std::move(parts);

    if (!object.layer_config_ranges.empty()) {
        json   ranges = json::array();
        size_t number = 1;
        for (const auto &range : object.layer_config_ranges)
            ranges.push_back({{"range", number++}, {"min_z", range.first.first}, {"max_z", range.first.second}, {"overrides", override_keys(range.second)}});
        out["height_ranges"] = std::move(ranges);
    }
    return out;
}
} // namespace

json model_objects_to_json(const Model &model)
{
    json objects = json::array();
    for (size_t i = 0; i < model.objects.size(); ++i)
        objects.push_back(object_to_json(*model.objects[i], i));
    return objects;
}

json slice_result_to_json(const GCodeProcessorResult &result)
{
    using Stats = PrintEstimatedStatistics;
    const Stats::Mode &mode = result.print_statistics.modes[static_cast<size_t>(Stats::ETimeMode::Normal)];

    json features = json::array();
    for (const auto &role : mode.roles_times)
        if (role.second > 0.f)
            features.push_back({{"feature", ExtrusionEntity::role_to_string(role.first)}, {"time_s", rounded(role.second, 10.0)}});

    json   filaments = json::array();
    double total_g   = 0.0;
    double total_m   = 0.0;
    for (const auto &used : result.print_statistics.total_volumes_per_extruder) {
        const size_t id       = used.first;
        const double diameter = id < result.filament_diameters.size() ? result.filament_diameters[id] : 0.0;
        const double density  = id < result.filament_densities.size() ? result.filament_densities[id] : 0.0;
        // mm^3 * g/cm^3 / 1000 gives grams; mm^3 / filament cross-section gives mm.
        const double grams  = used.second * density / 1000.0;
        const double area   = M_PI * diameter * diameter / 4.0;
        const double meters = area > 0.0 ? used.second / area / 1000.0 : 0.0;
        total_g += grams;
        total_m += meters;
        filaments.push_back({{"slot", id + 1}, {"used_g", rounded(grams)}, {"used_m", rounded(meters)}});
    }

    json warnings = json::array();
    for (const auto &warning : result.warnings)
        warnings.push_back({{"level", warning.level}, {"message", warning.msg}, {"code", warning.error_code}});

    json out = {
        {"time_s", rounded(mode.time, 1.0)},
        {"prepare_time_s", rounded(mode.prepare_time, 1.0)},
        {"layers", mode.layers_times.size()},
        {"total_g", rounded(total_g)},
        {"total_m", rounded(total_m)},
        {"filaments", std::move(filaments)},
        {"features", std::move(features)},
        {"warnings", std::move(warnings)},
    };
    if (result.toolpath_outside)
        out["toolpath_outside"] = true;
    if (result.gcode_check_result.error_code != 0)
        out["gcode_check_error_code"] = result.gcode_check_result.error_code;
    if (result.conflict_result)
        out["conflict"] = {{"object_a", result.conflict_result->_objName1}, {"object_b", result.conflict_result->_objName2}, {"height", result.conflict_result->_height}};
    return out;
}

}}} // namespace Slic3r::GUI::AgentBridge
