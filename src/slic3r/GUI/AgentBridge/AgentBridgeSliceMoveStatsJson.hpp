#ifndef slic3r_AgentBridgeSliceMoveStatsJson_hpp_
#define slic3r_AgentBridgeSliceMoveStatsJson_hpp_

#include <algorithm>
#include <vector>

#include <nlohmann/json.hpp>

#include "libslic3r/GCode/GCodeProcessor.hpp"

// One pass over the slice result's moves: counts and times of non-extrusion moves (travel, retract,
// unretract, wipe, seam, filament change), the preview legend ranges, max Z, and pause / colour
// change / custom G-code events. No GUI dependency.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// Walks the prefix-sum move times as a running maximum. Marker moves (seam, filament change, pause,
// custom G-code) keep time 0 unless Studio's post-processing filled them in, which it skips for
// re-parsed G-code (post-processing scripts, G-code preview), so plain differences would count the
// whole print again after each marker. Same rule as GCodeProcessor::update_preview_layers_times_stats.
class MoveClock
{
public:
    explicit MoveClock(const std::vector<GCodeProcessorResult::MoveVertex> &moves) : m_now(moves.empty() ? 0.f : moves.front().time[0]) {}
    // Time spent on this move; call once per move after the first, in order.
    float step(const GCodeProcessorResult::MoveVertex &move)
    {
        const float next = std::max(m_now, move.time[0]);
        const float spent = next - m_now;
        m_now = next;
        return spent;
    }
    // Time into the print at the last stepped move.
    float now() const { return m_now; }

private:
    float m_now;
};

// Returns {"moves": [...], "ranges": {...}, "max_z_mm": z, "custom_gcode": [...] (only when any)}.
nlohmann::json slice_move_stats_to_json(const GCodeProcessorResult &result, const PrintEstimatedStatistics::Mode &mode);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
