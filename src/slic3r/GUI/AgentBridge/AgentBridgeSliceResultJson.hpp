#ifndef slic3r_AgentBridgeSliceResultJson_hpp_
#define slic3r_AgentBridgeSliceResultJson_hpp_

#include <nlohmann/json.hpp>

#include "libslic3r/GCode/GCodeProcessor.hpp"

// Slice result summary for agents: every number the Studio preview panel shows (Line Type view,
// filament table, time summary, legend ranges). Pure function of the result, no GUI dependency.
// Slots count from 1, matching the Studio UI.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// Filament amount of one slot, in grams and meters.
struct FilamentAmount
{
    double g = 0.0;
    double m = 0.0;
};

// Converts an extruded volume (mm^3) of a 0-based slot to grams and meters, as the panel does.
FilamentAmount filament_from_volume(const GCodeProcessorResult &result, size_t slot, double volume_mm3);

// Rounds to 1/scale (scale 100 gives 2 decimals).
double rounded(double value, double scale = 100.0);

// Time, per-feature time and filament, per-slot filament split, cost, move counts, legend ranges and warnings.
nlohmann::json slice_result_to_json(const GCodeProcessorResult &result);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
