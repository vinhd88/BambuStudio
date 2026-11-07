// Opt-in get_slice_result detail: per-layer rows, per-object attribution, and the include /
// layer_from / layer_to arguments.
#include <catch2/catch.hpp>

#include <nlohmann/json.hpp>

#include "libslic3r/GCode/GCodeProcessor.hpp"

#include "slic3r/GUI/AgentBridge/AgentBridgeSliceBreakdownJson.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeTypes.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI::AgentBridge;
using nlohmann::json;

namespace {
using Move = GCodeProcessorResult::MoveVertex;

// A move at `time` (prefix sum) feeding `filament_mm` of filament into a 0-based slot.
Move move(EMoveType type, float print_z, float time, float filament_mm = 0.f, unsigned char slot = 0, int label = -1,
          ExtrusionRole role = erExternalPerimeter)
{
    Move m;
    m.type            = type;
    m.extrusion_role  = type == EMoveType::Extrude ? role : erNone;
    m.print_z         = print_z;
    m.position        = Vec3f(0.f, 0.f, print_z);
    m.height          = 0.2f;
    m.time[0]         = time;
    m.delta_extruder  = filament_mm;
    m.extruder_id     = slot;
    m.object_label_id = label;
    return m;
}

// Three layers; 1 mm of filament of diameter 2/sqrt(pi) is 1 mm^3, and density 1000 makes that 1 g.
void fill_three_layers(GCodeProcessorResult &result)
{
    const float diameter      = float(2.0 / std::sqrt(M_PI));
    result.filament_diameters = {diameter, diameter};
    result.filament_densities = {1000.f, 1000.f};
    auto &mode        = result.print_statistics.modes[0];
    mode.time         = 100.f;
    mode.prepare_time = 10.f;
    result.initial_layer_time   = 30.f; // Studio's first layer time without the start G-code
    result.label_object_enabled = true;
    result.moves = {
        move(EMoveType::Noop, 0.f, 0.f),
        move(EMoveType::Extrude, 0.f, 10.f, 5.f, 0, -1, erCustom), // start G-code purge line: prepare, no layer
        move(EMoveType::Extrude, 0.2f, 30.f, 2.f, 0, 7),           // layer 1
        move(EMoveType::Travel, 0.6f, 32.f, 0.f, 0, -1),           // z-hop travel does not start a layer
        move(EMoveType::Extrude, 0.2f, 40.f, 1.f, 1, 8),
        move(EMoveType::Extrude, 0.4f, 70.f, 3.f, 0, 7),           // layer 2
        move(EMoveType::Retract, 0.4f, 71.f),
        move(EMoveType::Extrude, 0.6f, 100.f, 4.f, 1, 8),          // layer 3
    };
}
} // namespace

TEST_CASE("breakdown arguments are validated", "[agent_bridge]")
{
    BreakdownRequest none = parse_breakdown_request(json::object());
    CHECK_FALSE(none.layers);
    CHECK_FALSE(none.objects);

    BreakdownRequest both = parse_breakdown_request({{"include", json::array({"layers", "objects"})}, {"layer_from", 2}, {"layer_to", 5}});
    CHECK(both.layers);
    CHECK(both.objects);
    CHECK(both.layer_from == 2);
    CHECK(both.layer_to == 5);

    CHECK_THROWS_AS(parse_breakdown_request({{"include", "layers"}}), BridgeError);
    CHECK_THROWS_AS(parse_breakdown_request({{"include", json::array({"speeds"})}}), BridgeError);
    CHECK_THROWS_AS(parse_breakdown_request({{"include", json::array({"layers"})}, {"layer_from", 0}}), BridgeError);
    CHECK_THROWS_AS(parse_breakdown_request({{"include", json::array({"layers"})}, {"layer_from", 5}, {"layer_to", 2}}), BridgeError);
    CHECK_THROWS_AS(parse_breakdown_request({{"layer_from", 1}}), BridgeError); // range without include layers
}

TEST_CASE("layers are split on print Z and use Studio's layer times when they agree", "[agent_bridge]")
{
    GCodeProcessorResult result;
    fill_three_layers(result);

    SECTION("layer count matches Studio's")
    {
        result.print_statistics.modes[0].layers_times = {40.f, 31.f, 29.f}; // first includes the 10 s start G-code
        const json out = slice_layers_to_json(result, 0, 0);
        const json &rows = out["layers_detail"];
        REQUIRE(rows.size() == 3);
        CHECK_FALSE(out.contains("layer_time_source"));
        CHECK(rows[0]["layer"] == 1);
        CHECK(rows[0]["z_mm"] == Approx(0.2));
        CHECK(rows[0]["height_mm"] == Approx(0.2));
        CHECK(rows[0]["time_s"] == Approx(30.0));
        CHECK(rows[0]["used_g"] == Approx(3.0));
        CHECK(rows[0]["slots"] == json::array({1, 2}));
        CHECK(rows[1]["time_s"] == Approx(31.0));
        CHECK(rows[1]["height_mm"] == Approx(0.2)); // Z step from the layer below
        CHECK(rows[1]["slots"] == json::array({1}));
        CHECK(rows[2]["used_g"] == Approx(4.0));
        CHECK(rows[2]["slots"] == json::array({2}));
    }
    SECTION("layer count differs: times come from moves and are flagged")
    {
        result.print_statistics.modes[0].layers_times = {40.f, 60.f};
        const json out = slice_layers_to_json(result, 0, 0);
        CHECK(out["layer_time_source"] == "moves");
        const json &rows = out["layers_detail"];
        REQUIRE(rows.size() == 3);
        CHECK(rows[0]["time_s"] == Approx(30.0)); // 10..40
        CHECK(rows[1]["time_s"] == Approx(31.0)); // 40..71
        CHECK(rows[2]["time_s"] == Approx(29.0)); // 71..100
    }
    SECTION("layer_from and layer_to pick rows")
    {
        const json rows = slice_layers_to_json(result, 2, 2)["layers_detail"];
        REQUIRE(rows.size() == 1);
        CHECK(rows[0]["layer"] == 2);
        CHECK(slice_layers_to_json(result, 3, 0)["layers_detail"].size() == 1);
        CHECK(slice_layers_to_json(result, 9, 0)["layers_detail"].empty());
    }
}

TEST_CASE("layer rows are capped", "[agent_bridge]")
{
    GCodeProcessorResult result;
    result.filament_diameters = {1.75f};
    result.filament_densities = {1.24f};
    result.moves.push_back(move(EMoveType::Noop, 0.f, 0.f));
    for (int i = 1; i <= MAX_LAYER_ROWS + 5; ++i)
        result.moves.push_back(move(EMoveType::Extrude, 0.2f * i, float(i), 1.f));
    const json out = slice_layers_to_json(result, 0, 0);
    CHECK(out["layers_detail"].size() == MAX_LAYER_ROWS);
    CHECK(out["layers_truncated"] == true);
}

TEST_CASE("objects get the time and filament of their labelled moves", "[agent_bridge]")
{
    GCodeProcessorResult result;
    fill_three_layers(result);
    const ObjectLabels labels = {{7, {1, 1, "Pen holder"}}, {8, {2, 1, "Lid"}}};

    const json out  = slice_objects_to_json(result, labels);
    const json &rows = out["objects_detail"];
    REQUIRE(rows.size() == 3);
    CHECK(rows[0]["name"] == "Pen holder");
    CHECK(rows[0]["object"] == 1);
    CHECK(rows[0]["instance"] == 1);
    CHECK(rows[0]["time_s"] == 50.0);       // 10..30 and 40..70
    CHECK(rows[0]["percent"] == 50.0);
    CHECK(rows[0]["used_g"] == Approx(5.0));
    CHECK(rows[1]["name"] == "Lid");
    CHECK(rows[1]["time_s"] == 37.0);       // 32..40 and 71..100
    CHECK(rows[1]["used_g"] == Approx(5.0));
    CHECK(rows[2]["name"] == "other");
    CHECK(rows[2]["time_s"] == 13.0);       // purge, travel, retract
    CHECK(rows[2]["used_g"] == Approx(5.0));
    CHECK(out.contains("objects_note"));

    SECTION("unknown label ids keep their number")
    {
        const json unnamed = slice_objects_to_json(result, {});
        CHECK(unnamed["objects_detail"][0]["name"] == "label 7");
    }
    SECTION("no labels in the G-code")
    {
        for (Move &m : result.moves)
            m.object_label_id = -1;
        const json off = slice_objects_to_json(result, labels);
        CHECK_FALSE(off.contains("objects_detail"));
        CHECK(off["objects_unavailable"].is_string());
    }
}

TEST_CASE("marker moves without a time do not count the print again", "[agent_bridge]")
{
    // Re-parsed G-code (post-processing scripts, G-code preview) leaves seam / pause markers at time 0.
    GCodeProcessorResult result;
    fill_three_layers(result);
    result.print_statistics.modes[0].layers_times = {40.f, 60.f}; // force the moves-based layer times
    result.moves.insert(result.moves.begin() + 3, move(EMoveType::Seam, 0.2f, 0.f, 0.f, 0, 7));
    result.moves.insert(result.moves.begin() + 7, move(EMoveType::Pause_Print, 0.4f, 0.f));

    const json objects = slice_objects_to_json(result, {})["objects_detail"];
    CHECK(objects[0]["time_s"] == 50.0);
    CHECK(objects[1]["time_s"] == 37.0);
    CHECK(objects[2]["time_s"] == 13.0);
    const json layers = slice_layers_to_json(result, 0, 0)["layers_detail"];
    CHECK(layers[0]["time_s"] == Approx(30.0));
    CHECK(layers[1]["time_s"] == Approx(31.0));
    CHECK(layers[2]["time_s"] == Approx(29.0));
}
