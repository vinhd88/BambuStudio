#include "AgentBridgeSliceMoveStatsJson.hpp"

#include <algorithm>
#include <array>
#include <limits>

#include "AgentBridgeSliceResultJson.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
using Move = GCodeProcessorResult::MoveVertex;

// Same cut-off the preview uses: shorter segments have noisy flow and inflate the legend max.
constexpr float MIN_SEGMENT_FOR_FLOW_MM = 0.05f;

struct Range
{
    float min = std::numeric_limits<float>::max();
    float max = std::numeric_limits<float>::lowest();
    void  add(float value)
    {
        min = std::min(min, value);
        max = std::max(max, value);
    }
    // Unset or all-zero ranges carry no information.
    bool empty() const { return min > max || (min == 0.f && max == 0.f); }
};

// Non-extrusion move rows, in the order the panel lists its options.
constexpr std::array<std::pair<EMoveType, const char *>, 6> MOVE_ROWS = {{
    {EMoveType::Travel, "travel"},
    {EMoveType::Retract, "retract"},
    {EMoveType::Unretract, "unretract"},
    {EMoveType::Wipe, "wipe"},
    {EMoveType::Seam, "seam"},
    {EMoveType::Tool_change, "filament_change"},
}};

const char *event_name(EMoveType type)
{
    switch (type) {
    case EMoveType::Color_change: return "color_change";
    case EMoveType::Pause_Print:  return "pause";
    case EMoveType::Custom_GCode: return "custom_gcode";
    default:                      return nullptr;
    }
}

json moves_to_json(const std::array<size_t, size_t(EMoveType::Count)> &counts, const PrintEstimatedStatistics::Mode &mode)
{
    json rows = json::array();
    for (const auto &[type, name] : MOVE_ROWS) {
        json row = {{"type", name}, {"count", counts[size_t(type)]}};
        const auto time = std::find_if(mode.moves_times.begin(), mode.moves_times.end(), [type = type](const auto &item) { return item.first == type; });
        if (time != mode.moves_times.end() && time->second > 0.f) {
            row["time_s"]  = rounded(time->second, 10.0);
            row["percent"] = mode.time > 0.f ? rounded(100.0 * time->second / mode.time) : 0.0;
        }
        if (row["count"] != 0 || row.contains("time_s"))
            rows.push_back(std::move(row));
    }
    return rows;
}
} // namespace

json slice_move_stats_to_json(const GCodeProcessorResult &result, const PrintEstimatedStatistics::Mode &mode)
{
    std::array<size_t, size_t(EMoveType::Count)> counts{};
    Range height, width, speed, fan, temperature, flow, layer_time;
    float max_z  = 0.f;
    json  events = json::array();

    const std::vector<Move> &moves = result.moves;
    MoveClock                clock(moves);
    // The preview skips the first vertex (the origin), so do the same.
    for (size_t i = 1; i < moves.size(); ++i) {
        const Move &move = moves[i];
        clock.step(move);
        if (move.type < EMoveType::Count)
            ++counts[size_t(move.type)];
        if (const char *name = event_name(move.type)) {
            events.push_back({{"type", name}, {"z_mm", rounded(move.position.z())}, {"at_time_s", rounded(clock.now(), 1.0)}, {"slot", int(move.extruder_id) + 1}});
            continue;
        }
        if (move.type != EMoveType::Extrude)
            continue;
        // Start G-code (custom role) extrudes odd heights and widths; the preview leaves them out too.
        const bool custom = move.extrusion_role == erCustom && !move.is_pa_line_calibration;
        if (!custom) {
            height.add(move.height);
            width.add(move.width);
            if ((move.position - moves[i - 1].position).norm() >= MIN_SEGMENT_FOR_FLOW_MM)
                flow.add(move.volumetric_rate());
        }
        fan.add(move.fan_speed);
        temperature.add(move.temperature);
        speed.add(move.feedrate);
        if (move.layer_duration > 0.f)
            layer_time.add(move.layer_duration);
        max_z = std::max(max_z, move.position.z());
    }

    json ranges = json::object();
    auto put    = [&ranges](const char *key, const Range &range, double scale) {
        if (!range.empty())
            ranges[key] = json::array({rounded(range.min, scale), rounded(range.max, scale)});
    };
    put("layer_height_mm", height, 1000.0);
    put("line_width_mm", width, 1000.0);
    put("speed_mm_s", speed, 10.0);
    put("fan_percent", fan, 1.0);
    put("temperature_c", temperature, 1.0);
    put("volumetric_mm3_s", flow, 100.0);
    put("layer_time_s", layer_time, 10.0);

    json out = {{"moves", moves_to_json(counts, mode)}, {"ranges", std::move(ranges)}, {"max_z_mm", rounded(max_z)}};
    if (!events.empty())
        out["custom_gcode"] = std::move(events);
    return out;
}

}}} // namespace Slic3r::GUI::AgentBridge
