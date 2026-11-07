#include "AgentBridgeProjectJson.hpp"

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

}}} // namespace Slic3r::GUI::AgentBridge
