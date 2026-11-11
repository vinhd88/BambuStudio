// Definition of one setting for describe_settings. Split from AgentBridgeSettingsJson.cpp.
#include "AgentBridgeSettingsJson.hpp"
#include "AgentBridgeSettingsTabMap.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
const char *type_name(ConfigOptionType type)
{
    switch (type) {
    case coFloat:            return "float";
    case coFloats:           return "float[]";
    case coInt:              return "int";
    case coInts:             return "int[]";
    case coString:           return "string";
    case coStrings:          return "string[]";
    case coPercent:          return "percent";
    case coPercents:         return "percent[]";
    case coFloatOrPercent:   return "float_or_percent";
    case coFloatsOrPercents: return "float_or_percent[]";
    case coPoint:            return "point";
    case coPoints:           return "point[]";
    case coPoint3:           return "point3";
    case coBool:             return "bool";
    case coBools:            return "bool[]";
    case coEnum:             return "enum";
    case coEnums:            return "enum[]";
    default:                 return "other";
    }
}
} // namespace

json setting_definition_to_json(const std::string &key, const ConfigOptionDef &def)
{
    json out = {{"key", key}, {"type", type_name(def.type)}};
    if (const SettingPlace *place = find_setting_place(key)) {
        out["tab"]  = place->tab;
        out["page"] = place->page;
        if (place->group[0] != '\0')
            out["group"] = place->group;
    }
    // Empty and sentinel fields are left out: this response is read by an LLM and tokens cost money.
    if (!def.label.empty())
        out["label"] = def.label;
    if (!def.sidetext.empty())
        out["unit"] = def.sidetext;
    if (!def.tooltip.empty())
        out["tooltip"] = def.tooltip;
    if (def.min != ConfigOptionDef::min_default)
        out["min"] = def.min;
    if (def.max != ConfigOptionDef::max_default)
        out["max"] = def.max;
    if (const ConfigOption *fallback = definition_default(def))
        out["default"] = option_to_json(*fallback);
    if (def.nullable)
        out["nullable"] = true;
    if (!def.enum_values.empty()) {
        out["enum"] = def.enum_values;
        if (def.enum_labels.size() == def.enum_values.size())
            out["enum_labels"] = def.enum_labels;
    }
    return out;
}

}}} // namespace Slic3r::GUI::AgentBridge
