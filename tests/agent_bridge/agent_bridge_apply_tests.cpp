#include <catch2/catch.hpp>

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeChangeJournal.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeSafetyRules.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeSettingValue.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI::AgentBridge;
using nlohmann::json;

namespace {
const ConfigOptionDef &def_of(const std::string &key)
{
    const ConfigOptionDef *def = print_config_def.get(key);
    REQUIRE(def != nullptr);
    return *def;
}

// The definition's default gives the same types Studio's own configs hold.
std::unique_ptr<ConfigOption> parse(const std::string &key, const json &value, std::string &error, const std::string &current_text = "")
{
    std::unique_ptr<ConfigOption> current(def_of(key).default_value->clone());
    if (!current_text.empty())
        REQUIRE(current->deserialize(current_text));
    return parse_setting_value(def_of(key), *current, value, error);
}

std::string parsed_text(const std::string &key, const json &value, const std::string &current_text = "")
{
    std::string error;
    auto        option = parse(key, value, error, current_text);
    INFO(key << ": " << error);
    REQUIRE(option != nullptr);
    return option->serialize();
}

std::string rejection(const std::string &key, const json &value, const std::string &current_text = "")
{
    std::string error;
    auto        option = parse(key, value, error, current_text);
    INFO(key << " should be rejected");
    CHECK(option == nullptr);
    CHECK_FALSE(error.empty());
    return error;
}

SafetyLimits a1_limits()
{
    SafetyLimits limits;
    limits.nozzle_temp_low  = 190;
    limits.nozzle_temp_high = 240;
    limits.bed_temp_max     = 100;
    limits.max_speed_xy     = 500;
    limits.max_speed_z      = 20;
    limits.max_accel_xy     = 10000;
    limits.max_jerk_xy      = 20;
    return limits;
}

std::optional<std::string> unsafe(const std::string &key, const json &value, const std::string &current_text = "")
{
    std::string error;
    auto        option = parse(key, value, error, current_text);
    INFO(key << ": " << error);
    REQUIRE(option != nullptr);
    return value_rejection(key, def_of(key), *option, a1_limits());
}
} // namespace

TEST_CASE("parse_setting_value accepts each value form", "[agent_bridge]")
{
    CHECK(parsed_text("enable_support", true) == "1");
    CHECK(parsed_text("enable_support", false) == "0");
    CHECK(parsed_text("layer_height", 0.12) == "0.12");
    CHECK(parsed_text("sparse_infill_density", "15%") == "15%");
    CHECK(parsed_text("sparse_infill_density", 15) == "15%");
    CHECK(parsed_text("sparse_infill_pattern", "gyroid") == "gyroid");
    // Either a number or a percentage.
    CHECK(parsed_text("seam_slope_start_height", 0.1) == "0.1");
    CHECK(parsed_text("seam_slope_start_height", "10%") == "10%");
}

TEST_CASE("parse_setting_value keeps the shape of list settings", "[agent_bridge]")
{
    SECTION("a single value sets every element")
    {
        CHECK(parsed_text("outer_wall_speed", 120, "60,80") == "120,120");
    }
    SECTION("a list sets each element")
    {
        CHECK(parsed_text("outer_wall_speed", json::array({100, 90}), "60,80") == "100,90");
    }
    SECTION("a list of another length is refused")
    {
        CHECK(rejection("outer_wall_speed", json::array({100, 90, 80}), "60,80").find("list of 2") != std::string::npos);
    }
    SECTION("a single value leaves empty elements empty")
    {
        CHECK(parsed_text("outer_wall_speed", 120, "60,nil") == "120,nil");
    }
    SECTION("a single value fills a list that is all empty")
    {
        CHECK(parsed_text("outer_wall_speed", 120, "nil,nil") == "120,120");
    }
    SECTION("null clears an element only where the setting allows it")
    {
        CHECK(parsed_text("outer_wall_speed", json::array({100, nullptr}), "60,80") == "100,nil");
        CHECK(rejection("filament_type", json::array({nullptr}), "\"PLA\"").find("empty") != std::string::npos);
    }
    SECTION("text lists keep text with separators intact")
    {
        std::string error;
        auto        option = parse("filament_type", "PLA,CF", error, "\"PLA\"");
        REQUIRE(option != nullptr);
        const auto &values = static_cast<const ConfigOptionStrings &>(*option).values;
        REQUIRE(values.size() == 1);
        CHECK(values[0] == "PLA,CF");
    }
    SECTION("the current value is not modified")
    {
        std::unique_ptr<ConfigOption> current(def_of("outer_wall_speed").default_value->clone());
        const std::string             before = current->serialize();
        std::string                   error;
        parse_setting_value(def_of("outer_wall_speed"), *current, 999, error);
        CHECK(current->serialize() == before);
    }
}

TEST_CASE("parse_setting_value rejects values that do not fit", "[agent_bridge]")
{
    CHECK(rejection("enable_support", "yes").find("true or false") != std::string::npos);
    CHECK(rejection("enable_support", json::array({true})).find("single value") != std::string::npos);
    CHECK(rejection("layer_height", "thin").find("number") != std::string::npos);
    CHECK(rejection("wall_loops", 2.5).find("whole number") != std::string::npos);
    CHECK(rejection("sparse_infill_density", "lots").find("percentage") != std::string::npos);
    CHECK(rejection("sparse_infill_pattern", "not_a_pattern").find("gyroid") != std::string::npos);
    CHECK(rejection("seam_slope_start_height", "-5%").find("negative") != std::string::npos);
}

TEST_CASE("parse_setting_value enforces the definition's limits", "[agent_bridge]")
{
    CHECK(rejection("sparse_infill_density", 150).find("maximum") != std::string::npos);
    CHECK(rejection("sparse_infill_density", -1).find("minimum") != std::string::npos);
    CHECK(rejection("outer_wall_speed", -10, "60").find("minimum") != std::string::npos);
}

TEST_CASE("key_rejection covers every read-only group", "[agent_bridge]")
{
    SECTION("the plan's read-only keys")
    {
        for (const char *key : {"machine_start_gcode", "machine_end_gcode", "layer_change_gcode", "filament_start_gcode", "machine_max_speed_x", "machine_max_acceleration_x",
                                "printable_area", "printable_height", "filename_format"}) {
            INFO(key);
            REQUIRE(key_rejection(key, ApplyScope::Global));
            CHECK(key_rejection(key, ApplyScope::Global)->find("read-only") != std::string::npos);
        }
    }
    SECTION("a filament's temperature range cannot be widened, which would lift its own limit")
    {
        CHECK(key_rejection("nozzle_temperature_range_high", ApplyScope::Global)->find("read-only") != std::string::npos);
        CHECK(key_rejection("nozzle_temperature_range_low", ApplyScope::Global)->find("read-only") != std::string::npos);
    }
    SECTION("the whole printer tab is read-only")
    {
        CHECK(key_rejection("nozzle_diameter", ApplyScope::Global)->find("printer") != std::string::npos);
        CHECK(key_rejection("retraction_length", ApplyScope::Global)->find("printer") != std::string::npos);
    }
    SECTION("hidden keys look the same as keys that do not exist")
    {
        CHECK(*key_rejection("no_such_setting", ApplyScope::Global) == "unknown setting");
        CHECK(*key_rejection("post_process", ApplyScope::Global) == "unknown setting");
        CHECK(*key_rejection("print_host", ApplyScope::Global) == "unknown setting");
        CHECK(*key_rejection("inherits", ApplyScope::Global) == "unknown setting");
    }
    SECTION("process and filament keys are allowed")
    {
        CHECK_FALSE(key_rejection("outer_wall_speed", ApplyScope::Global));
        CHECK_FALSE(key_rejection("nozzle_temperature", ApplyScope::Global));
    }
}

TEST_CASE("key_rejection follows what each level offers", "[agent_bridge]")
{
    CHECK_FALSE(key_rejection("curr_bed_type", ApplyScope::Plate));
    CHECK_FALSE(key_rejection("spiral_mode", ApplyScope::Plate));
    CHECK(key_rejection("outer_wall_speed", ApplyScope::Plate));
    // These open Studio's sequence dialog.
    CHECK(key_rejection("first_layer_sequence_choice", ApplyScope::Plate));

    CHECK_FALSE(key_rejection("outer_wall_speed", ApplyScope::Object));
    CHECK_FALSE(key_rejection("enable_support", ApplyScope::Object));
    CHECK(key_rejection("nozzle_temperature", ApplyScope::Object));

    CHECK_FALSE(key_rejection("sparse_infill_density", ApplyScope::Part));
    CHECK(key_rejection("enable_support", ApplyScope::Part));

    CHECK_FALSE(key_rejection("layer_height", ApplyScope::HeightRange));
    CHECK_FALSE(key_rejection("sparse_infill_density", ApplyScope::HeightRange));
    CHECK(key_rejection("enable_support", ApplyScope::HeightRange));
}

TEST_CASE("value_rejection applies filament and machine limits", "[agent_bridge]")
{
    SECTION("nozzle temperature must stay in the filament's range")
    {
        CHECK_FALSE(unsafe("nozzle_temperature", 220, "220"));
        CHECK_FALSE(unsafe("nozzle_temperature", 240, "220"));
        REQUIRE(unsafe("nozzle_temperature", 260, "220"));
        CHECK(unsafe("nozzle_temperature", 260, "220")->find("above") != std::string::npos);
        CHECK(unsafe("nozzle_temperature_initial_layer", 150, "220")->find("below") != std::string::npos);
    }
    SECTION("bed temperature must stay under the printer's limit")
    {
        CHECK_FALSE(unsafe("hot_plate_temp", 65, "55"));
        CHECK(unsafe("hot_plate_temp", 120, "55"));
        CHECK(unsafe("textured_plate_temp_initial_layer", 120, "55"));
    }
    SECTION("speeds and accelerations must stay under machine limits")
    {
        CHECK_FALSE(unsafe("outer_wall_speed", 200, "60"));
        CHECK(unsafe("outer_wall_speed", 600, "60"));
        CHECK(unsafe("travel_speed", 700, "500"));
        CHECK_FALSE(unsafe("default_acceleration", 6000, "6000"));
        CHECK(unsafe("default_acceleration", 20000, "6000"));
        CHECK(unsafe("outer_wall_acceleration", 20000, "500"));
        CHECK(unsafe("default_jerk", 25, "9"));
        CHECK(unsafe("travel_speed_z", 30, "12"));
    }
    SECTION("every element of a list is checked")
    {
        CHECK(unsafe("outer_wall_speed", json::array({100, 600}), "60,80"));
    }
    SECTION("percentages are relative, so machine limits do not apply")
    {
        // No mm/s setting takes a percentage today, so the rule is checked with a built option.
        const ConfigOptionFloatOrPercent percent(600, true);
        const ConfigOptionFloatOrPercent absolute(600, false);
        CHECK_FALSE(value_rejection("outer_wall_speed", def_of("outer_wall_speed"), percent, a1_limits()));
        CHECK(value_rejection("outer_wall_speed", def_of("outer_wall_speed"), absolute, a1_limits()));
    }
    SECTION("an unknown limit does not reject")
    {
        std::string error;
        auto        option = parse("outer_wall_speed", 9000, error, "60");
        REQUIRE(option != nullptr);
        CHECK_FALSE(value_rejection("outer_wall_speed", def_of("outer_wall_speed"), *option, SafetyLimits{}));
    }
}

TEST_CASE("value_warning flags lowered cooling only", "[agent_bridge]")
{
    const auto warning = [](const std::string &key, const std::string &before_text, const json &value) {
        std::unique_ptr<ConfigOption> before(def_of(key).default_value->clone());
        REQUIRE(before->deserialize(before_text));
        std::string error;
        auto        after = parse_setting_value(def_of(key), *before, value, error);
        REQUIRE(after != nullptr);
        return value_warning(key, *before, *after);
    };
    CHECK(warning("slow_down_layer_time", "8", 4));
    CHECK(warning("fan_max_speed", "100", 60));
    CHECK_FALSE(warning("slow_down_layer_time", "8", 12));
    CHECK_FALSE(warning("outer_wall_speed", "100", 60));
    CHECK(warning("fan_min_speed", "20", 10));
    CHECK(warning("fan_cooling_layer_time", "10", 5));
    CHECK(warning("additional_cooling_fan_speed", "60", 40));
}

TEST_CASE("parse_setting_value handles null and malformed percentages", "[agent_bridge]")
{
    SECTION("a scalar setting refuses null")
    {
        CHECK(rejection("layer_height", nullptr).find("single value") != std::string::npos);
    }
    SECTION("a single null clears a nullable list, and is refused for a non-nullable one")
    {
        CHECK(parsed_text("outer_wall_speed", nullptr, "60,80") == "nil,nil");
        CHECK(rejection("filament_type", nullptr, "\"PLA\"").find("empty") != std::string::npos);
    }
    SECTION("malformed percentages are refused")
    {
        CHECK(rejection("seam_slope_start_height", "inf%").find("percentage") != std::string::npos);
        CHECK(rejection("seam_slope_start_height", "15 %").find("percentage") != std::string::npos);
        CHECK(rejection("seam_slope_start_height", "%").find("percentage") != std::string::npos);
    }
}

TEST_CASE("safety messages name the value and the limit", "[agent_bridge]")
{
    CHECK(unsafe("nozzle_temperature", 260, "220")->find("260 C is above this filament's range, which ends at 240 C") != std::string::npos);
    CHECK(unsafe("nozzle_temperature_initial_layer", 150, "220")->find("150 C is below this filament's range, which starts at 190 C") != std::string::npos);
    CHECK(unsafe("outer_wall_speed", 600, "60")->find("600 mm/s is above the printer's speed limit of 500 mm/s") != std::string::npos);
}

TEST_CASE("a zero limit means unknown and does not reject", "[agent_bridge]")
{
    SafetyLimits limits = a1_limits();
    limits.bed_temp_max = 0;
    std::string error;
    auto        option = parse("hot_plate_temp", 120, error, "55");
    REQUIRE(option != nullptr);
    CHECK_FALSE(value_rejection("hot_plate_temp", def_of("hot_plate_temp"), *option, limits));
}

TEST_CASE("ChangeJournal", "[agent_bridge]")
{
    ChangeJournal journal;
    journal.set_project("a");

    const auto value = [](const char *key) { return JournalValue{key, std::make_shared<ConfigOptionFloat>(1.)}; };

    SECTION("records before-values and their outcome")
    {
        const std::string id = journal.begin({{"scope", "global"}}, {value("layer_height")});
        REQUIRE(journal.last() != nullptr);
        CHECK(journal.last()->outcome["status"] == "running");
        journal.add_values(id, {value("detect_overhang_wall")});
        journal.finish(id, {{"status", "applied"}});
        CHECK(journal.last()->outcome["status"] == "applied");
        CHECK(journal.last()->values.size() == 2);
    }
    SECTION("a key that was not set is recorded as absent")
    {
        journal.begin({}, {JournalValue{"layer_height", nullptr}});
        CHECK(journal.last()->values[0].before == nullptr);
    }
    SECTION("the same project keeps its records")
    {
        journal.begin({}, {});
        journal.set_project("a");
        CHECK(journal.records().size() == 1);
    }
    SECTION("another project clears them but does not reuse ids")
    {
        const std::string first = journal.begin({}, {});
        journal.set_project("b");
        CHECK(journal.records().empty());
        CHECK(journal.last() == nullptr);
        CHECK(journal.begin({}, {}) != first);
    }
    SECTION("late calls for a forgotten change are ignored")
    {
        const std::string id = journal.begin({}, {});
        journal.set_project("b");
        journal.add_values(id, {value("x")});
        journal.finish(id, {{"status", "applied"}});
        CHECK(journal.records().empty());
    }
    SECTION("old records make way for new ones")
    {
        for (int i = 0; i < 600; ++i)
            journal.begin({}, {});
        CHECK(journal.records().size() == 500);
    }
}

TEST_CASE("review fixes: limits on units that also take a percentage", "[agent_bridge]")
{
    // Units "mm/s or %" and "mm/s2 or %": a plain number is a speed or acceleration.
    CHECK(unsafe("small_perimeter_speed", 5000, "50%"));
    CHECK(unsafe("vertical_shell_speed", 5000, "80%"));
    CHECK(unsafe("sparse_infill_acceleration", 100000, "100%"));
    CHECK_FALSE(unsafe("small_perimeter_speed", "300%", "50%"));
    CHECK_FALSE(unsafe("sparse_infill_acceleration", 5000, "100%"));
}

TEST_CASE("review fixes: plate keys are not taken at global scope", "[agent_bridge]")
{
    for (const char *key : {"first_layer_sequence_choice", "other_layers_sequence_choice"}) {
        INFO(key);
        REQUIRE(key_rejection(key, ApplyScope::Global));
        CHECK(key_rejection(key, ApplyScope::Global)->find("scope plate") != std::string::npos);
    }
    CHECK_FALSE(key_rejection("curr_bed_type", ApplyScope::Plate));
}

TEST_CASE("the bed type is taken at global scope, as the project-wide one", "[agent_bridge]")
{
    CHECK_FALSE(key_rejection("curr_bed_type", ApplyScope::Global));
    CHECK(key_rejection("curr_bed_type", ApplyScope::Object));
}

TEST_CASE("review fixes: text list elements keep separators and quotes", "[agent_bridge]")
{
    for (const std::string text : {"A;B", "\"x\"", "  lead", "a,b;c"}) {
        INFO(text);
        std::string error;
        auto        option = parse("filament_type", text, error, "\"PLA\"");
        REQUIRE(option != nullptr);
        const auto &values = static_cast<const ConfigOptionStrings &>(*option).values;
        REQUIRE(values.size() == 1);
        CHECK(values[0] == text);
    }
}
