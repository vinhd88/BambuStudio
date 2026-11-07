#include "AgentBridgeSliceBreakdownJson.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "AgentBridgeSliceMoveStatsJson.hpp"
#include "AgentBridgeSliceResultJson.hpp"
#include "AgentBridgeTypes.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
using Move = GCodeProcessorResult::MoveVertex;

int positive_int_arg(const json &args, const char *name)
{
    const auto it = args.find(name);
    if (it == args.end() || it->is_null())
        return 0;
    if (!it->is_number_integer() || it->get<int64_t>() < 1 || it->get<int64_t>() > 1000000)
        throw BridgeError(400, "invalid_args", std::string(name) + " must be a whole number starting at 1");
    return static_cast<int>(it->get<int64_t>());
}

// Grams of filament for `length_mm` of filament fed into a 0-based slot.
double grams_of(const GCodeProcessorResult &result, size_t slot, float length_mm)
{
    const double diameter = slot < result.filament_diameters.size() ? result.filament_diameters[slot] : 0.0;
    return filament_from_volume(result, slot, length_mm * M_PI * diameter * diameter / 4.0).g;
}

// Filament fed by a move, or 0 for anything but a forward extrusion.
double extruded_g(const GCodeProcessorResult &result, const Move &move)
{
    return move.type == EMoveType::Extrude && move.delta_extruder > 0.f ? grams_of(result, move.extruder_id, move.delta_extruder) : 0.0;
}

struct LayerTotals
{
    float          z      = 0.f;
    float          height = 0.f;
    double         time   = 0.0;
    double         grams  = 0.0;
    std::set<int>  slots;
};

struct Totals
{
    double time  = 0.0;
    double grams = 0.0;
};
} // namespace

BreakdownRequest parse_breakdown_request(const json &args)
{
    BreakdownRequest request;
    const auto include = args.find("include");
    if (include != args.end() && !include->is_null()) {
        if (!include->is_array())
            throw BridgeError(400, "invalid_args", "include must be a list such as [\"layers\", \"objects\"]");
        for (const json &item : *include) {
            const std::string name = item.is_string() ? item.get<std::string>() : std::string();
            if (name == "layers")
                request.layers = true;
            else if (name == "objects")
                request.objects = true;
            else
                throw BridgeError(400, "invalid_args", "include accepts only \"layers\" and \"objects\"");
        }
    }
    request.layer_from = positive_int_arg(args, "layer_from");
    request.layer_to   = positive_int_arg(args, "layer_to");
    if (request.layer_from > 0 && request.layer_to > 0 && request.layer_from > request.layer_to)
        throw BridgeError(400, "invalid_args", "layer_from must not be greater than layer_to");
    if ((request.layer_from > 0 || request.layer_to > 0) && !request.layers)
        throw BridgeError(400, "invalid_args", "layer_from and layer_to need include [\"layers\"]");
    return request;
}

json slice_layers_to_json(const GCodeProcessorResult &result, int layer_from, int layer_to)
{
    const std::vector<Move> &moves = result.moves;
    std::vector<LayerTotals> layers;
    MoveClock                clock(moves);
    for (size_t i = 1; i < moves.size(); ++i) {
        const Move &move  = moves[i];
        const float spent = clock.step(move);
        // A layer starts where model extrusion moves to another print Z (the per-layer Z tag; position
        // Z before the first tag). Start G-code lines (custom role) never start one, and z-hops are travel.
        if (move.type == EMoveType::Extrude && move.extrusion_role != erCustom) {
            const float z = move.print_z > 0.f ? move.print_z : move.position.z();
            if (layers.empty() || std::abs(z - layers.back().z) > 1e-4f) {
                // Studio's layer height is the Z step; a drop (next object of a by-object print) or the
                // first layer falls back to the line's own height.
                const float step = layers.empty() ? 0.f : z - layers.back().z;
                layers.push_back({z, step > 1e-4f ? step : (layers.empty() ? z : move.height)});
            }
        }
        // Moves before the first layer are prepare time.
        if (layers.empty())
            continue;
        LayerTotals &layer = layers.back();
        layer.time += spent;
        if (const double grams = extruded_g(result, move); grams > 0.0) {
            layer.grams += grams;
            layer.slots.insert(int(move.extruder_id) + 1);
        }
    }

    // The slider's own layer times are exact; use them when the layer split agrees with Studio's.
    const auto &mode       = result.print_statistics.modes[size_t(PrintEstimatedStatistics::ETimeMode::Normal)];
    const bool  use_slider = !layers.empty() && layers.size() == mode.layers_times.size();

    const int first = std::max(1, layer_from);
    const int last  = layer_to > 0 ? std::min<int>(layer_to, int(layers.size())) : int(layers.size());
    json      rows  = json::array();
    bool      truncated = false;
    for (int number = first; number <= last; ++number) {
        if (int(rows.size()) == MAX_LAYER_ROWS) {
            truncated = true;
            break;
        }
        const LayerTotals &layer = layers[size_t(number - 1)];
        double time = layer.time;
        if (use_slider)
            // Studio's first entry includes the start G-code; its first layer time (first_layer_time_s,
            // legend layer time) leaves that out.
            time = number == 1 ? result.initial_layer_time : mode.layers_times[size_t(number - 1)];
        rows.push_back({{"layer", number},
                        {"z_mm", rounded(layer.z, 1000.0)},
                        {"height_mm", rounded(layer.height, 1000.0)},
                        {"time_s", rounded(time, 10.0)},
                        {"used_g", rounded(layer.grams, 1000.0)},
                        {"slots", layer.slots}});
    }

    json out = {{"layers_detail", std::move(rows)}};
    if (!use_slider)
        out["layer_time_source"] = "moves"; // layer split differs from Studio's (e.g. vase mode); times summed from moves
    if (truncated)
        out["layers_truncated"] = true;
    return out;
}

json slice_objects_to_json(const GCodeProcessorResult &result, const ObjectLabels &labels)
{
    const std::vector<Move> &moves = result.moves;
    std::map<int, Totals> per_label;
    Totals                other;
    MoveClock             clock(moves);
    for (size_t i = 1; i < moves.size(); ++i) {
        const Move  &move  = moves[i];
        const float  spent = clock.step(move);
        Totals      &totals = move.object_label_id >= 0 ? per_label[move.object_label_id] : other;
        totals.time += spent;
        totals.grams += extruded_g(result, move);
    }
    // Re-parsed G-code clears label_object_enabled but its moves keep the labels, so the moves decide.
    if (per_label.empty())
        // GCode.cpp labels objects only for 2 to 64 object instances, outside calibration prints.
        return {{"objects_unavailable", "the G-code has no object labels; Studio writes them only when the plate has 2 to 64 object instances"}};

    const auto  &mode    = result.print_statistics.modes[size_t(PrintEstimatedStatistics::ETimeMode::Normal)];
    auto         percent = [&mode](double time) { return mode.time > 0.f ? rounded(100.0 * time / mode.time) : 0.0; };
    json         rows    = json::array();
    for (const auto &[label, totals] : per_label) {
        json row;
        if (const auto it = labels.find(label); it != labels.end())
            row = {{"object", it->second.object}, {"instance", it->second.instance}, {"name", it->second.name}};
        else
            row = {{"name", "label " + std::to_string(label)}};
        row["time_s"]       = rounded(totals.time, 1.0);
        row["percent"]      = percent(totals.time);
        row["used_g"]       = rounded(totals.grams);
        rows.push_back(std::move(row));
    }
    rows.push_back({{"name", "other"}, {"time_s", rounded(other.time, 1.0)}, {"percent", percent(other.time)}, {"used_g", rounded(other.grams)}});

    return {{"objects_detail", std::move(rows)},
            {"objects_note", "time and filament of moves labelled with each object; other is prepare, travel between objects, "
                             "prime tower, flushing and anything unlabelled. Rows sum to time_s, but object rows are not the panel's numbers."}};
}

}}} // namespace Slic3r::GUI::AgentBridge
