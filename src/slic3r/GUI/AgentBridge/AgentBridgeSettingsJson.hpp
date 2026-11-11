#ifndef slic3r_AgentBridgeSettingsJson_hpp_
#define slic3r_AgentBridgeSettingsJson_hpp_

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"

// Converts Studio configs to compact JSON, grouped the way the UI groups them. No GUI dependency.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// "process", or "process/Speed". Case-insensitive; empty matches everything.
struct SettingsFilter
{
    std::string tab;
    std::string page;
    bool        include_defaults = false;
    // Drop keys the tab map places on a tab other than the one being listed.
    bool        own_tab_only     = false;

    // Throws BridgeError invalid_args for an unknown tab.
    static SettingsFilter parse(const std::string &category, bool include_defaults);
};

// False for credentials and print host details, which must never leave the bridge. Includes
// post_process: its script command lines can carry API keys and reveal local paths.
bool is_setting_exposed(const std::string &key);

// False for bookkeeping keys (inherits, *_settings_id, ...) that are not settings.
bool is_user_setting(const std::string &key);

// A setting's default as Studio builds it (ConfigOptionDef::create_default_option). Use this, never
// def.default_value directly: for enum lists that one has no name table, and serializing it crashes.
// Null if the setting has no default. Cached for the process; definitions never change.
const ConfigOption *definition_default(const ConfigOptionDef &def);

nlohmann::json option_to_json(const ConfigOption &option);

// First element of a string-list option, or "" if absent or empty. Use instead of opt_string(key, 0),
// which is undefined behaviour on an empty list, as a preset with `"filament_vendor": ""` can produce.
std::string first_string_value(const DynamicPrintConfig &config, const std::string &key);

// Settings grouped as { page: { group: { key: entry } } }. A key that differs from `base`, or is
// absent from it, has entry { value, base }; other keys appear only with include_defaults, as the
// bare value. `tab` places keys that have no place in the tab map. `base` may be null.
nlohmann::json settings_to_json(const ConfigBase &values, const ConfigBase *base, const std::string &tab, const SettingsFilter &filter);

// Definition of one setting for describe_settings: type, unit, limits, enum values, tooltip.
nlohmann::json setting_definition_to_json(const std::string &key, const ConfigOptionDef &def);

// Keys of the print config in the filter's tab and page. Skips hidden and bookkeeping keys.
std::vector<std::string> keys_in_category(const SettingsFilter &filter);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
