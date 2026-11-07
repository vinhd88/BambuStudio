// Panel-parity fields of get_slice_result: time split, per-feature filament, per-slot split and cost,
// move counts, legend ranges and custom G-code events. The base fields are covered in agent_bridge_json_tests.cpp.
#include <catch2/catch.hpp>

#include <nlohmann/json.hpp>

#include "libslic3r/GCode/GCodeProcessor.hpp"

#include "slic3r/GUI/AgentBridge/AgentBridgeSliceResultJson.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI::AgentBridge;
using nlohmann::json;

namespace {
using Move = GCodeProcessorResult::MoveVertex;

Move extrude(float x, float z, float height, float width, float feedrate, float fan, float temperature, float layer_duration = 0.f,
             ExtrusionRole role = erExternalPerimeter)
{
    Move move;
    move.type           = EMoveType::Extrude;
    move.extrusion_role = role;
    move.position       = Vec3f(x, 0.f, z);
    move.height         = height;
    move.width          = width;
    move.feedrate       = feedrate;
    move.mm3_per_mm     = height * width;
    move.fan_speed      = fan;
    move.temperature    = temperature;
    move.layer_duration = layer_duration;
    return move;
}

Move other(EMoveType type, float z = 0.f, float time = 0.f, unsigned char extruder = 0)
{
    Move move;
    move.type        = type;
    move.position    = Vec3f(0.f, 0.f, z);
    move.time[0]     = time;
    move.extruder_id = extruder;
    return move;
}

const json *find_row(const json &rows, const char *key, const char *value)
{
    for (const json &row : rows)
        if (row[key] == value)
            return &row;
    return nullptr;
}

void fill_two_slots(GCodeProcessorResult &result)
{
    result.filament_diameters = {1.75f, 1.75f};
    result.filament_densities = {1.0f, 1.0f};
    result.filament_costs     = {20.f, 30.f};
    auto &stats = result.print_statistics;
    // Grams equal mm^3 / 1000 with density 1.
    stats.total_volumes_per_extruder      = {{0, 10000.0}, {1, 4000.0}};
    stats.model_volumes_per_extruder      = {{0, 7000.0}, {1, 1500.0}};
    stats.support_volumes_per_extruder    = {{0, 1000.0}};
    stats.flush_per_filament              = {{1, 500.0}};
    stats.wipe_tower_volumes_per_extruder = {{0, 2000.0}, {1, 1500.0}};
}
} // namespace

TEST_CASE("slice result time split matches the preview panel", "[agent_bridge]")
{
    GCodeProcessorResult result;
    auto &mode         = result.print_statistics.modes[0];
    mode.time          = 7730.f;
    mode.prepare_time  = 376.f;
    result.initial_layer_time = 95.04f;

    SECTION("timelapse time is taken out of model time")
    {
        result.skippable_part_time[SkipType::stTimelapse] = 654.f;
        const json out = slice_result_to_json(result);
        CHECK(out["timelapse_time_s"] == 654.0);
        CHECK(out["model_time_s"] == 6700.0);
        CHECK(out["first_layer_time_s"] == Approx(95.0));
        CHECK(out["model_time_s"].get<double>() + out["prepare_time_s"].get<double>() + out["timelapse_time_s"].get<double>() ==
              out["time_s"].get<double>());
    }
    SECTION("no timelapse")
    {
        const json out = slice_result_to_json(result);
        CHECK(out["timelapse_time_s"] == 0.0);
        CHECK(out["model_time_s"] == 7354.0);
    }
}

TEST_CASE("slice result features carry percent and filament", "[agent_bridge]")
{
    GCodeProcessorResult result;
    auto &mode        = result.print_statistics.modes[0];
    mode.time         = 1000.f;
    mode.roles_times  = {{erNone, 300.f}, {erExternalPerimeter, 274.f}, {erInternalInfill, 100.f}};
    // Stored as (meters, grams).
    result.print_statistics.used_filaments_per_role = {{erExternalPerimeter, {9.24, 27.57}}};

    const json out = slice_result_to_json(result);
    const json *wall = find_row(out["features"], "feature", "Outer wall");
    REQUIRE(wall != nullptr);
    CHECK((*wall)["percent"] == Approx(27.4));
    CHECK((*wall)["used_g"] == Approx(27.57));
    CHECK((*wall)["used_m"] == Approx(9.24));
    CHECK(out["features"].size() == 2); // no row for erNone (travel time)
    const json *infill = find_row(out["features"], "feature", "Sparse infill");
    REQUIRE(infill != nullptr);
    CHECK((*infill)["used_g"] == 0.0);
}

TEST_CASE("slice result filaments split model, support, flushed and tower", "[agent_bridge]")
{
    GCodeProcessorResult result;
    fill_two_slots(result);
    const json out = slice_result_to_json(result);

    REQUIRE(out["filaments"].size() == 2);
    const json &first = out["filaments"][0];
    CHECK(first["slot"] == 1);
    CHECK(first["used_g"] == Approx(10.0));
    CHECK(first["support_g"] == Approx(1.0));
    CHECK(first["tower_g"] == Approx(2.0));
    CHECK(first["flushed_g"] == 0.0);
    CHECK(first["model_g"] == Approx(7.0));
    CHECK(first["cost"] == Approx(0.2)); // 10 g at 20 per kg
    CHECK(first["total_g"] == Approx(10.0));
    // Panel columns come from their own maps, so the column sum can differ from the processor total.
    const json &second = out["filaments"][1];
    CHECK(second["used_g"] == Approx(4.0));
    CHECK(second["model_g"] == Approx(1.5));
    CHECK(second["total_g"] == Approx(3.5));
    CHECK(second["cost"] == Approx(0.12));

    CHECK(out["total_g"] == Approx(14.0));
    CHECK(out["model_g"] == Approx(9.0));
    CHECK(out["support_g"] == Approx(1.0));
    CHECK(out["flushed_g"] == Approx(0.5));
    CHECK(out["tower_g"] == Approx(3.5));
    CHECK(out["cost"] == Approx(0.32));
    CHECK(out["model_m"].get<double>() < out["total_m"].get<double>());

    SECTION("only slots with filament use get a row")
    {
        result.print_statistics.flush_per_filament[2] = 300.0; // a slot with no total entry
        CHECK(slice_result_to_json(result)["filaments"].size() == 2);
    }
}

TEST_CASE("slice result counts moves and builds legend ranges", "[agent_bridge]")
{
    GCodeProcessorResult result;
    auto &mode       = result.print_statistics.modes[0];
    mode.time        = 100.f;
    mode.moves_times = {{EMoveType::Travel, 5.f}, {EMoveType::Extrude, 90.f}};

    result.moves = {
        other(EMoveType::Noop),                                         // origin, skipped
        extrude(0.f, 0.2f, 0.5f, 3.0f, 5.f, 0.f, 250.f, 0.f, erCustom), // start G-code line, kept out of height/width
        extrude(10.f, 0.2f, 0.2f, 0.45f, 50.f, 0.f, 220.f, 40.f),
        other(EMoveType::Retract),
        other(EMoveType::Travel),
        other(EMoveType::Unretract),
        other(EMoveType::Pause_Print, 0.2f, 61.f, 1),
        extrude(20.f, 0.4f, 0.2f, 0.42f, 200.f, 100.f, 230.f, 8.f),
        other(EMoveType::Retract),
    };

    const json out = slice_result_to_json(result);

    const json *travel = find_row(out["moves"], "type", "travel");
    REQUIRE(travel != nullptr);
    CHECK((*travel)["count"] == 1);
    CHECK((*travel)["time_s"] == 5.0);
    CHECK((*travel)["percent"] == 5.0);
    const json *retract = find_row(out["moves"], "type", "retract");
    REQUIRE(retract != nullptr);
    CHECK((*retract)["count"] == 2);
    CHECK_FALSE(retract->contains("time_s"));
    CHECK(find_row(out["moves"], "type", "seam") == nullptr); // nothing to report

    const json &ranges = out["ranges"];
    CHECK(ranges["layer_height_mm"] == json::array({0.2, 0.2}));
    CHECK(ranges["line_width_mm"] == json::array({0.42, 0.45}));
    CHECK(ranges["speed_mm_s"] == json::array({5.0, 200.0}));
    CHECK(ranges["fan_percent"] == json::array({0.0, 100.0}));
    CHECK(ranges["temperature_c"] == json::array({220.0, 250.0}));
    CHECK(ranges["layer_time_s"] == json::array({8.0, 40.0}));
    CHECK(ranges.contains("volumetric_mm3_s"));
    CHECK(out["max_z_mm"] == Approx(0.4));

    REQUIRE(out["custom_gcode"].size() == 1);
    CHECK(out["custom_gcode"][0]["type"] == "pause");
    CHECK(out["custom_gcode"][0]["z_mm"] == Approx(0.2));
    CHECK(out["custom_gcode"][0]["at_time_s"] == 61.0);
    CHECK(out["custom_gcode"][0]["slot"] == 2);

    SECTION("no moves gives no ranges and no events")
    {
        result.moves.clear();
        const json empty = slice_result_to_json(result);
        CHECK(empty["ranges"].empty());
        CHECK_FALSE(empty.contains("custom_gcode"));
    }
}
