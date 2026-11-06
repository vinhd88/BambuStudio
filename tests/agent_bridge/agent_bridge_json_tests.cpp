#include <catch2/catch.hpp>

#include <nlohmann/json.hpp>

#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "slic3r/GUI/AgentBridge/AgentBridgeProjectJson.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeArgs.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeSettingsJson.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeSettingsTabMap.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeTypes.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI::AgentBridge;
using nlohmann::json;

namespace {
DynamicPrintConfig config_from(std::initializer_list<std::pair<const char *, const char *>> values)
{
    DynamicPrintConfig config;
    for (const auto &value : values)
        config.set_deserialize_strict(value.first, value.second);
    return config;
}

SettingsFilter changed_only() { return SettingsFilter{}; }
} // namespace

TEST_CASE("option_to_json gives each type a natural JSON form", "[agent_bridge]")
{
    const DynamicPrintConfig config = config_from({
        {"layer_height", "0.2"},
        {"wall_loops", "3"},
        {"enable_support", "1"},
        {"sparse_infill_density", "15%"},
        {"sparse_infill_pattern", "gyroid"},
        {"outer_wall_speed", "60,70"},
        {"nozzle_temperature", "220,230"},
        {"machine_start_gcode", "G28"},
    });
    CHECK(option_to_json(*config.option("layer_height")) == 0.2);
    CHECK(option_to_json(*config.option("wall_loops")) == 3);
    CHECK(option_to_json(*config.option("enable_support")) == true);
    CHECK(option_to_json(*config.option("sparse_infill_density")) == "15%");
    CHECK(option_to_json(*config.option("sparse_infill_pattern")) == "gyroid");
    CHECK(option_to_json(*config.option("outer_wall_speed")) == json::array({60.0, 70.0}));
    CHECK(option_to_json(*config.option("nozzle_temperature")) == json::array({220, 230}));
    CHECK(option_to_json(*config.option("machine_start_gcode")) == "G28");
}

TEST_CASE("settings_to_json reports differences, grouped as in the UI", "[agent_bridge]")
{
    const DynamicPrintConfig values = config_from({{"layer_height", "0.2"}, {"wall_loops", "3"}, {"sparse_infill_density", "25%"}});
    const DynamicPrintConfig base   = config_from({{"layer_height", "0.2"}, {"wall_loops", "2"}, {"sparse_infill_density", "25%"}});

    SECTION("changed values by default, with the base value")
    {
        const json out = settings_to_json(values, &base, "process", changed_only());
        CHECK(out == json{{"Strength", {{"Walls", {{"wall_loops", {{"value", 3}, {"base", 2}}}}}}}});
    }
    SECTION("no base means nothing is reported as changed")
    {
        CHECK(settings_to_json(values, nullptr, "process", changed_only()).empty());
    }
    SECTION("include_defaults adds unchanged values as bare values")
    {
        const json out = settings_to_json(values, &base, "process", SettingsFilter::parse("", true));
        CHECK(out["Quality"]["Layer height"]["layer_height"] == 0.2);
        CHECK(out["Strength"]["Walls"]["wall_loops"]["value"] == 3);
        CHECK(out["Strength"]["Sparse infill"]["sparse_infill_density"] == "25%");
    }
    SECTION("a key missing from the base counts as changed, with a null base")
    {
        const DynamicPrintConfig sparse_base = config_from({{"layer_height", "0.2"}});
        const json out = settings_to_json(values, &sparse_base, "process", changed_only());
        CHECK(out["Strength"]["Walls"]["wall_loops"]["base"].is_null());
    }
    SECTION("category limits by page, case-insensitively")
    {
        const SettingsFilter filter = SettingsFilter::parse("process/quality", true);
        const json out = settings_to_json(values, &base, "process", filter);
        CHECK(out.size() == 1);
        CHECK(out.contains("Quality"));
    }
    SECTION("own_tab_only drops keys that belong to another tab, such as stray filament keys in a process preset")
    {
        const DynamicPrintConfig with_stray = config_from({{"layer_height", "0.2"}, {"pre_start_fan_time", "0"}});
        SettingsFilter           filter     = SettingsFilter::parse("", true);
        CHECK(settings_to_json(with_stray, nullptr, "process", filter).contains("Cooling"));
        filter.own_tab_only = true;
        const json out      = settings_to_json(with_stray, nullptr, "process", filter);
        CHECK(out.contains("Quality"));
        CHECK_FALSE(out.contains("Cooling"));
        CHECK(settings_to_json(with_stray, nullptr, "filament", filter).contains("Cooling"));
    }
    SECTION("category for another tab returns nothing")
    {
        CHECK(settings_to_json(values, &base, "process", SettingsFilter::parse("filament", true)).empty());
    }
}

TEST_CASE("SettingsFilter::parse", "[agent_bridge]")
{
    CHECK(SettingsFilter::parse("", false).tab.empty());
    const SettingsFilter filter = SettingsFilter::parse("process/Speed", true);
    CHECK(filter.tab == "process");
    CHECK(filter.page == "Speed");
    CHECK(filter.include_defaults);
    CHECK_THROWS_AS(SettingsFilter::parse("bogus/Speed", false), BridgeError);
}

TEST_CASE("credentials never leave the bridge", "[agent_bridge]")
{
    for (const char *key : {"post_process", "printhost_apikey", "printhost_password", "printhost_user", "printhost_cafile", "printhost_port", "print_host", "print_host_webui", "host_type"}) {
        CAPTURE(key);
        CHECK_FALSE(is_setting_exposed(key));
    }

    const DynamicPrintConfig values = config_from({{"printhost_apikey", "SECRET"}, {"print_host", "SECRET"}, {"printhost_password", "SECRET"}});
    const std::string out = settings_to_json(values, nullptr, "printer", SettingsFilter::parse("", true)).dump();
    CHECK(out.find("SECRET") == std::string::npos);

    for (const std::string &tab : {"process", "filament", "printer"})
        for (const std::string &key : keys_in_category(SettingsFilter::parse(tab, true))) {
            CAPTURE(key);
            CHECK(is_setting_exposed(key));
        }
}

TEST_CASE("first_string_value survives what opt_string(key, 0) does not", "[agent_bridge]")
{
    DynamicPrintConfig config = config_from({{"filament_type", "PETG"}});
    CHECK(first_string_value(config, "filament_type") == "PETG");
    CHECK(first_string_value(config, "filament_vendor").empty());

    config.set_key_value("filament_vendor", new ConfigOptionStrings());
    CHECK(first_string_value(config, "filament_vendor").empty());
}

TEST_CASE("optional_index_arg converts 1-based numbers and rejects bad ones", "[agent_bridge]")
{
    const auto code_of = [](const json &args) {
        try {
            optional_index_arg(args, "plate", 3);
            return std::string();
        } catch (const BridgeError &e) {
            return e.code;
        }
    };
    CHECK(optional_index_arg(json::object(), "plate", 3) == -1);
    CHECK(optional_index_arg({{"plate", nullptr}}, "plate", 3) == -1);
    CHECK(optional_index_arg({{"plate", 1}}, "plate", 3) == 0);
    CHECK(optional_index_arg({{"plate", 3}}, "plate", 3) == 2);
    CHECK(code_of({{"plate", 4}}) == "not_found");
    CHECK(code_of({{"plate", 0}}) == "invalid_args");
    CHECK(code_of({{"plate", -1}}) == "invalid_args");
    CHECK(code_of({{"plate", 1.5}}) == "invalid_args");
    CHECK(code_of({{"plate", "1"}}) == "invalid_args");
    CHECK(code_of({{"plate", true}}) == "invalid_args");
    CHECK(code_of({{"plate", 99999999999LL}}) == "not_found");
}

TEST_CASE("tab map places settings on their real pages", "[agent_bridge]")
{
    const auto place_of = [](const char *key) {
        const SettingPlace *place = find_setting_place(key);
        REQUIRE(place != nullptr);
        return std::string(place->tab) + "/" + place->page + "/" + place->group;
    };
    // One from each page built by a different code path in Tab.cpp.
    CHECK(place_of("layer_height") == "process/Quality/Layer height");
    CHECK(place_of("sparse_infill_density") == "process/Strength/Sparse infill");
    CHECK(place_of("enable_support") == "process/Support/Support");
    CHECK(place_of("nozzle_temperature") == "filament/Filament/Print temperature");
    CHECK(place_of("fan_max_speed") == "filament/Cooling/Part cooling fan");
    CHECK(place_of("additional_cooling_fan_speed") == "filament/Cooling/Auxiliary part cooling fan");
    CHECK(place_of("filament_retraction_length") == "filament/Setting Overrides/Retraction");
    CHECK(place_of("printable_area") == "printer/Basic information/Printable space");
    CHECK(place_of("machine_start_gcode") == "printer/Machine gcode/Machine start G-code");
    CHECK(place_of("machine_max_jerk_e") == "printer/Motion ability/Jerk limitation");
    CHECK(place_of("retraction_length") == "printer/Extruder/Retraction");
    CHECK(find_setting_place("no_such_key") == nullptr);
    // Sits inside `#if 0` in Tab.cpp: not in the UI, so not on the map.
    CHECK(find_setting_place("before_layer_change_gcode") == nullptr);
}

TEST_CASE("every mapped key is a real setting", "[agent_bridge]")
{
    size_t mapped = 0;
    for (const auto &option : print_config_def.options)
        if (find_setting_place(option.first) != nullptr)
            ++mapped;
    // The generator validates against source text; this checks the result against the live definitions.
    CHECK(mapped > 400);
    CHECK(keys_in_category(SettingsFilter::parse("process/Speed", true)).size() > 10);
    CHECK(keys_in_category(SettingsFilter::parse("process/Nonexistent", true)).empty());
}

TEST_CASE("setting_definition_to_json", "[agent_bridge]")
{
    SECTION("numeric setting has limits, unit and place")
    {
        const json def = setting_definition_to_json("layer_height", *print_config_def.get("layer_height"));
        CHECK(def["key"] == "layer_height");
        CHECK(def["type"] == "float");
        CHECK(def["tab"] == "process");
        CHECK(def["page"] == "Quality");
        CHECK(def["unit"] == "mm");
        CHECK(def.contains("tooltip"));
        CHECK(def.contains("default"));
    }
    SECTION("enum setting lists its values")
    {
        const json def = setting_definition_to_json("sparse_infill_pattern", *print_config_def.get("sparse_infill_pattern"));
        CHECK(def["type"] == "enum");
        const auto values = def["enum"].get<std::vector<std::string>>();
        CHECK(std::find(values.begin(), values.end(), "gyroid") != values.end());
    }
    SECTION("sentinel limits are omitted to save tokens")
    {
        const json def = setting_definition_to_json("machine_start_gcode", *print_config_def.get("machine_start_gcode"));
        CHECK_FALSE(def.contains("min"));
        CHECK_FALSE(def.contains("max"));
    }
}

TEST_CASE("model_objects_to_json", "[agent_bridge]")
{
    SECTION("empty project") { CHECK(model_objects_to_json(Model()) == json::array()); }

    SECTION("objects, parts, overrides and height ranges count from 1")
    {
        Model        model;
        ModelObject *object = model.add_object();
        object->name        = "Cube";
        object->add_instance();
        object->add_instance();
        object->config.set_key_value("wall_loops", new ConfigOptionInt(5));

        ModelVolume *part = object->add_volume(make_cube(10, 10, 10));
        part->name        = "Body";
        part->config.set_key_value("extruder", new ConfigOptionInt(2));
        part->config.set_key_value("sparse_infill_density", new ConfigOptionPercent(40));

        ModelVolume *modifier = object->add_volume(make_cube(5, 5, 5), ModelVolumeType::PARAMETER_MODIFIER);
        modifier->name        = "Dense area";

        ModelConfig range_config;
        range_config.set_key_value("layer_height", new ConfigOptionFloat(0.1));
        object->layer_config_ranges[{1.0, 2.0}] = range_config;

        const json out = model_objects_to_json(model);
        REQUIRE(out.size() == 1);
        const json &first = out[0];
        CHECK(first["object"] == 1);
        CHECK(first["name"] == "Cube");
        CHECK(first["instances"] == 2);
        CHECK(first["overrides"] == json::array({"wall_loops"}));

        REQUIRE(first["parts"].size() == 2);
        CHECK(first["parts"][0]["part"] == 1);
        CHECK(first["parts"][0]["type"] == "part");
        CHECK(first["parts"][0]["filament"] == 2);
        CHECK(first["parts"][0]["overrides"] == json::array({"extruder", "sparse_infill_density"}));
        CHECK(first["parts"][1]["type"] == "modifier");
        CHECK_FALSE(first["parts"][1].contains("filament"));
        CHECK_FALSE(first["parts"][1].contains("overrides"));

        REQUIRE(first["height_ranges"].size() == 1);
        CHECK(first["height_ranges"][0]["range"] == 1);
        CHECK(first["height_ranges"][0]["min_z"] == 1.0);
        CHECK(first["height_ranges"][0]["overrides"] == json::array({"layer_height"}));
    }
}

TEST_CASE("slice_result_to_json", "[agent_bridge]")
{
    GCodeProcessorResult result;
    auto &mode = result.print_statistics.modes[0];
    mode.time         = 3600.f;
    mode.prepare_time = 120.f;
    mode.layers_times = {1.f, 2.f, 3.f};
    mode.roles_times  = {{erExternalPerimeter, 1000.f}, {erIroning, 0.f}};
    result.filament_diameters = {1.75f, 1.75f};
    result.filament_densities = {1.25f, 1.27f};
    // 2405.28 mm^3 of 1.75 mm filament is about 1 m.
    result.print_statistics.total_volumes_per_extruder = {{0, 2405.28}, {1, 1000.0}};
    result.warnings.push_back({1, "too_hot", "E1", {}});

    const json out = slice_result_to_json(result);
    CHECK(out["time_s"] == 3600.0);
    CHECK(out["prepare_time_s"] == 120.0);
    CHECK(out["layers"] == 3);

    REQUIRE(out["filaments"].size() == 2);
    CHECK(out["filaments"][0]["slot"] == 1);
    CHECK(out["filaments"][0]["used_g"] == Approx(3.01));
    CHECK(out["filaments"][0]["used_m"] == Approx(1.0));
    CHECK(out["filaments"][1]["slot"] == 2);
    CHECK(out["filaments"][1]["used_g"] == Approx(1.27));
    CHECK(out["total_g"] == Approx(4.28));

    REQUIRE(out["features"].size() == 1); // zero-time features are dropped
    CHECK(out["features"][0]["time_s"] == 1000.0);
    REQUIRE(out["warnings"].size() == 1);
    CHECK(out["warnings"][0]["code"] == "E1");

    CHECK_FALSE(out.contains("toolpath_outside"));
    CHECK_FALSE(out.contains("conflict"));

    SECTION("problems are reported only when present")
    {
        result.toolpath_outside = true;
        result.gcode_check_result.error_code = 8;
        result.conflict_result = ConflictResult("A", "B", 3.f, nullptr, nullptr);
        const json bad = slice_result_to_json(result);
        CHECK(bad["toolpath_outside"] == true);
        CHECK(bad["gcode_check_error_code"] == 8);
        CHECK(bad["conflict"]["object_a"] == "A");
    }
}
