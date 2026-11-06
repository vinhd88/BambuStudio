#include "AgentBridgeSettingValue.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include <boost/algorithm/string.hpp>

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
const char *const NIL_TEXT = "nil";

std::optional<double> parse_number(const std::string &text)
{
    try {
        size_t      used  = 0;
        const double value = std::stod(text, &used);
        return used == text.size() && std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

bool is_percent_text(const std::string &text)
{
    return text.size() > 1 && text.back() == '%' && parse_number(text.substr(0, text.size() - 1)).has_value();
}

// One element in the text form Studio's config files use. Empty optional and `error` set if it does not fit.
std::optional<std::string> element_text(const ConfigOptionDef &def, ConfigOptionType element_type, const json &value, std::string &error)
{
    switch (element_type) {
    case coBool:
        if (value.is_boolean())
            return value.get<bool>() ? "1" : "0";
        error = "must be true or false";
        break;
    case coInt:
        if (value.is_number_integer())
            return value.dump();
        error = "must be a whole number";
        break;
    case coFloat:
        if (value.is_number())
            return value.dump();
        error = "must be a number";
        break;
    case coPercent:
        if (value.is_number())
            return value.dump() + "%";
        if (value.is_string() && is_percent_text(value.get<std::string>()))
            return value.get<std::string>();
        error = "must be a number or a percentage such as \"15%\"";
        break;
    case coFloatOrPercent:
        if (value.is_number())
            return value.dump();
        if (value.is_string() && is_percent_text(value.get<std::string>()))
            return value.get<std::string>();
        error = "must be a number or a percentage such as \"50%\"";
        break;
    case coString:
        if (value.is_string())
            return value.get<std::string>();
        error = "must be text";
        break;
    case coEnum:
        if (value.is_string() && std::find(def.enum_values.begin(), def.enum_values.end(), value.get<std::string>()) != def.enum_values.end())
            return value.get<std::string>();
        error = "must be one of: " + boost::algorithm::join(def.enum_values, ", ");
        break;
    default:
        error = "cannot be set through the bridge";
        break;
    }
    return std::nullopt;
}

// Text for a whole list, keeping its length. `value` is one element for all, or a list.
std::optional<std::string> vector_text(const ConfigOptionDef &def, const ConfigOptionVectorBase &current, const json &value, std::string &error)
{
    const auto   element_type = static_cast<ConfigOptionType>(def.type & ~coVectorType);
    const size_t size         = current.size();

    if (value.is_array() && value.size() != size) {
        error = "must be a single value or a list of " + std::to_string(size);
        return std::nullopt;
    }
    bool has_any_value = false;
    for (size_t i = 0; i < size; ++i)
        has_any_value = has_any_value || !current.is_nil(i);

    std::vector<std::string> items;
    for (size_t i = 0; i < size; ++i) {
        const json &item = value.is_array() ? value[i] : value;
        if (item.is_null() || (!value.is_array() && has_any_value && current.is_nil(i))) {
            if (!def.nullable) {
                error = "cannot be empty";
                return std::nullopt;
            }
            items.emplace_back(NIL_TEXT);
            continue;
        }
        const auto text = element_text(def, element_type, item, error);
        if (!text)
            return std::nullopt;
        // Quoted, so separators and quotes inside the text stay part of it.
        items.push_back(element_type == coString ? "\"" + escape_string_cstyle(*text) + "\"" : *text);
    }
    // Studio separates list elements with commas, except text lists, whose elements can hold commas.
    return boost::algorithm::join(items, element_type == coString ? ";" : ",");
}

std::string limit_text(double limit, const ConfigOptionDef &def)
{
    json number = limit;
    return number.dump() + (def.sidetext.empty() ? "" : " " + def.sidetext);
}

// The definition's limits apply to plain numbers. For a value that is either, its percentage is
// relative to something else, so those limits do not apply.
bool within_limits(const ConfigOptionDef &def, const ConfigOption &option, std::string &error)
{
    const auto element_type = static_cast<ConfigOptionType>(def.type & ~coVectorType);
    for (const NumericValue &number : numeric_values(option)) {
        if (number.is_percent && element_type == coFloatOrPercent) {
            if (number.value < 0) {
                error = "cannot be a negative percentage";
                return false;
            }
            continue;
        }
        if (def.min != ConfigOptionDef::min_default && number.value < def.min) {
            error = "is below the minimum of " + limit_text(def.min, def);
            return false;
        }
        if (def.max != ConfigOptionDef::max_default && number.value > def.max) {
            error = "is above the maximum of " + limit_text(def.max, def);
            return false;
        }
    }
    return true;
}
} // namespace

std::vector<NumericValue> numeric_values(const ConfigOption &option)
{
    const int element_type = option.type() & ~coVectorType;
    if (element_type != coInt && element_type != coFloat && element_type != coPercent && element_type != coFloatOrPercent)
        return {};

    std::vector<std::string> items;
    if (option.is_vector())
        items = static_cast<const ConfigOptionVectorBase &>(option).vserialize();
    else
        items = {option.serialize()};

    std::vector<NumericValue> out;
    for (const std::string &item : items) {
        const bool percent = !item.empty() && item.back() == '%';
        // nil and other spellings of "no value" do not parse.
        if (const auto number = parse_number(percent ? item.substr(0, item.size() - 1) : item))
            out.push_back({*number, percent});
    }
    return out;
}

std::unique_ptr<ConfigOption> parse_setting_value(const ConfigOptionDef &def, const ConfigOption &current, const json &value, std::string &error)
{
    std::optional<std::string> text;
    if (current.is_vector()) {
        text = vector_text(def, static_cast<const ConfigOptionVectorBase &>(current), value, error);
    } else if (value.is_array() || value.is_null()) {
        error = "must be a single value";
    } else {
        text = element_text(def, current.type(), value, error);
    }
    if (!text)
        return nullptr;

    std::unique_ptr<ConfigOption> option(current.clone());
    if (current.type() == coString) {
        // deserialize would strip quotes and unescape; plain text must stay as given.
        static_cast<ConfigOptionString &>(*option).value = *text;
    } else if (!option->deserialize(*text)) {
        error = "has a value Studio cannot read";
        return nullptr;
    }
    if (current.is_vector() && static_cast<const ConfigOptionVectorBase &>(*option).size() != static_cast<const ConfigOptionVectorBase &>(current).size()) {
        error = "has a value Studio cannot read as a list of " + std::to_string(static_cast<const ConfigOptionVectorBase &>(current).size());
        return nullptr;
    }
    return within_limits(def, *option, error) ? std::move(option) : nullptr;
}

}}} // namespace Slic3r::GUI::AgentBridge
