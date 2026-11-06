#ifndef slic3r_AgentBridgeSafetyRules_hpp_
#define slic3r_AgentBridgeSafetyRules_hpp_

#include <optional>
#include <string>

#include "libslic3r/Config.hpp"

// What an agent may change, and to what. Enforced here in the bridge rather than the adapter, so
// every client path is covered. No GUI dependency.
namespace Slic3r { namespace GUI { namespace AgentBridge {

enum class ApplyScope { Global, Plate, Object, Part, HeightRange };

// Limits taken from the current filament and printer. Zero or unset means the limit is unknown.
struct SafetyLimits
{
    std::optional<int> nozzle_temp_low;
    std::optional<int> nozzle_temp_high;
    int                bed_temp_max   = 0;
    double             max_speed_xy   = 0;
    double             max_speed_z    = 0;
    double             max_accel_xy   = 0;
    double             max_jerk_xy    = 0;
};

// Why a key can never be set at this scope, or nullopt if it can. Secret and internal keys get the
// same answer as ones that do not exist, so a caller cannot tell whether they exist.
std::optional<std::string> key_rejection(const std::string &key, ApplyScope scope);

// Why this value is unsafe, or nullopt if it is fine. Assumes the value already passed
// parse_setting_value. Percentages are relative, so machine limits do not apply to them.
std::optional<std::string> value_rejection(const std::string &key, const ConfigOptionDef &def, const ConfigOption &value, const SafetyLimits &limits);

// A caution to report with an accepted change, or nullopt.
std::optional<std::string> value_warning(const std::string &key, const ConfigOption &before, const ConfigOption &after);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
