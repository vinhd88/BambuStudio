#include "AgentBridgeSettingsJson.hpp"

#include <cmath>
#include <set>

#include <boost/algorithm/string.hpp>

#include "libslic3r/PrintConfig.hpp"

#include "AgentBridgeSettingsTabMap.hpp"
#include "AgentBridgeTypes.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
const char *const UNPLACED_PAGE    = "Other";
const char *const UNNAMED_GROUP    = "General";

json string_to_number_or_null(const std::string &text, bool integer)
{
    try {
        if (integer)
            return std::stoll(text);
        const double value = std::stod(text);
        return std::isfinite(value) ? json(value) : json(nullptr);
    } catch (const std::exception &) {
        // nil and other non-numeric spellings
        return nullptr;
    }
}

json vector_option_to_json(const ConfigOption &option)
{
    const auto *vec = dynamic_cast<const ConfigOptionVectorBase *>(option.is_vector() ? &option : nullptr);
    if (vec == nullptr)
        return option.serialize();

    std::vector<std::string> items;
    try {
        items = vec->vserialize();
    } catch (const std::exception &) {
        // Some group-valued options cannot be split per element.
        return option.serialize();
    }

    const int element_type = option.type() & ~coVectorType;
    json      out          = json::array();
    for (size_t i = 0; i < items.size(); ++i) {
        if (i < vec->size() && vec->is_nil(i))
            out.push_back(nullptr);
        else if (element_type == coFloat)
            out.push_back(string_to_number_or_null(items[i], false));
        else if (element_type == coInt)
            out.push_back(string_to_number_or_null(items[i], true));
        else if (element_type == coBool)
            out.push_back(items[i] == "1");
        else
            out.push_back(items[i]);
    }
    return out;
}

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

bool matches_filter(const SettingPlace *place, const std::string &fallback_tab, const SettingsFilter &filter)
{
    const std::string tab  = place != nullptr ? place->tab : fallback_tab;
    const std::string page = place != nullptr ? place->page : UNPLACED_PAGE;
    return (filter.tab.empty() || boost::iequals(filter.tab, tab)) && (filter.page.empty() || boost::iequals(filter.page, page));
}
} // namespace

SettingsFilter SettingsFilter::parse(const std::string &category, bool include_defaults)
{
    SettingsFilter filter;
    filter.include_defaults = include_defaults;
    if (category.empty())
        return filter;

    const size_t slash = category.find('/');
    filter.tab         = category.substr(0, slash);
    filter.page        = slash == std::string::npos ? std::string() : category.substr(slash + 1);
    if (!boost::iequals(filter.tab, "process") && !boost::iequals(filter.tab, "filament") && !boost::iequals(filter.tab, "printer"))
        throw BridgeError(400, "invalid_args", "category must start with process, filament or printer, e.g. process/Speed");
    return filter;
}

bool is_setting_exposed(const std::string &key)
{
    return !boost::starts_with(key, "printhost") && !boost::starts_with(key, "print_host") && key != "host_type" && key != "post_process";
}

bool is_user_setting(const std::string &key)
{
    static const std::set<std::string> bookkeeping = {
        "inherits", "compatible_printers", "compatible_printers_condition", "compatible_prints", "compatible_prints_condition",
        "print_settings_id", "filament_settings_id", "printer_settings_id", "different_settings_to_system", "print_compatible_printers",
        "filament_ids", "filament_vendor_id", "from", "name", "type", "version", "setting_id", "base_id", "instantiation", "user_id", "updated_time",
    };
    return is_setting_exposed(key) && bookkeeping.count(key) == 0;
}

std::string first_string_value(const DynamicPrintConfig &config, const std::string &key)
{
    const auto *option = config.option<ConfigOptionStrings>(key);
    return option != nullptr && !option->values.empty() ? option->values.front() : std::string();
}

json option_to_json(const ConfigOption &option)
{
    if (option.is_vector())
        return vector_option_to_json(option);

    switch (option.type()) {
    case coBool:   return option.getBool();
    case coInt:    return option.getInt();
    case coFloat:  { const double value = option.getFloat(); return std::isfinite(value) ? json(value) : json(nullptr); }
    case coString: return static_cast<const ConfigOptionString &>(option).value;
    // Percent, enum and point are clearest as the text the UI and config files use: "15%", "gyroid".
    default:       return option.serialize();
    }
}

json settings_to_json(const ConfigBase &values, const ConfigBase *base, const std::string &tab, const SettingsFilter &filter)
{
    json pages = json::object();
    for (const std::string &key : values.keys()) {
        if (!is_user_setting(key))
            continue;
        const SettingPlace *place = find_setting_place(key);
        if (!matches_filter(place, tab, filter) || (filter.own_tab_only && place != nullptr && tab != place->tab))
            continue;

        const ConfigOption *value      = values.option(key);
        const ConfigOption *base_value = base != nullptr ? base->option(key) : nullptr;
        const bool          changed    = base != nullptr && (base_value == nullptr || *value != *base_value);
        if (!changed && !filter.include_defaults)
            continue;

        json entry;
        if (changed)
            entry = {{"value", option_to_json(*value)}, {"base", base_value != nullptr ? option_to_json(*base_value) : json(nullptr)}};
        else
            entry = option_to_json(*value);

        const std::string page  = place != nullptr ? place->page : UNPLACED_PAGE;
        const std::string group = place != nullptr && place->group[0] != '\0' ? place->group : UNNAMED_GROUP;
        pages[page][group][key] = std::move(entry);
    }
    return pages;
}

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
    if (def.default_value)
        out["default"] = option_to_json(*def.default_value);
    if (def.nullable)
        out["nullable"] = true;
    if (!def.enum_values.empty()) {
        out["enum"] = def.enum_values;
        if (def.enum_labels.size() == def.enum_values.size())
            out["enum_labels"] = def.enum_labels;
    }
    return out;
}

std::vector<std::string> keys_in_category(const SettingsFilter &filter)
{
    std::vector<std::string> keys;
    for (const auto &option : print_config_def.options) {
        const SettingPlace *place = find_setting_place(option.first);
        // Unplaced keys have no tab, so they cannot be asked for by category.
        if (place != nullptr && is_user_setting(option.first) && matches_filter(place, std::string(), filter))
            keys.push_back(option.first);
    }
    return keys;
}

}}} // namespace Slic3r::GUI::AgentBridge
