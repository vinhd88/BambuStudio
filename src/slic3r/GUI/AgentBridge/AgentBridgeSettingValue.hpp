#ifndef slic3r_AgentBridgeSettingValue_hpp_
#define slic3r_AgentBridgeSettingValue_hpp_

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "libslic3r/Config.hpp"

// Turns a JSON value from a client into a config option, using the same forms get_settings returns.
// No GUI dependency.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// `current` gives the shape to keep: a per-variant list stays the same length, and a single value
// sets every element that has a value. Returns null and sets `error` if the value does not fit the
// setting's type, allowed values or min and max. The result is independent of `current`.
std::unique_ptr<ConfigOption> parse_setting_value(const ConfigOptionDef &def, const ConfigOption &current, const nlohmann::json &value, std::string &error);

// One number of an option's value. List elements without a value (nil) are left out.
struct NumericValue
{
    double value;
    bool   is_percent;
};
std::vector<NumericValue> numeric_values(const ConfigOption &option);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
