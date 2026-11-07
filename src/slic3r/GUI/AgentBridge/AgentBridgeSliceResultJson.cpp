#include "AgentBridgeSliceResultJson.hpp"

#include <cmath>

#include "libslic3r/ExtrusionEntity.hpp"

#include "AgentBridgeSliceMoveStatsJson.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

double rounded(double value, double scale) { return std::round(value * scale) / scale; }

FilamentAmount filament_from_volume(const GCodeProcessorResult &result, size_t slot, double volume_mm3)
{
    const double diameter = slot < result.filament_diameters.size() ? result.filament_diameters[slot] : 0.0;
    const double density  = slot < result.filament_densities.size() ? result.filament_densities[slot] : 0.0;
    const double area     = M_PI * diameter * diameter / 4.0;
    // mm^3 * g/cm^3 / 1000 gives grams; mm^3 / filament cross-section gives mm.
    return {volume_mm3 * density / 1000.0, area > 0.0 ? volume_mm3 / area / 1000.0 : 0.0};
}

namespace {
using Stats = PrintEstimatedStatistics;

double volume_of(const std::map<size_t, double> &volumes, size_t slot)
{
    const auto it = volumes.find(slot);
    return it == volumes.end() ? 0.0 : it->second;
}

// Per-slot rows with the panel's Model / Support / Flushed / Tower / Total columns, and plate totals.
// Per slot the panel reads each column from its own volume map and shows their sum as Total; that
// sum can differ from used_g (the processor's total), which does not always count the first flush.
// Plate-level model is the panel's "Model Filament": total minus support, flushed and tower.
void add_filaments(const GCodeProcessorResult &result, json &out)
{
    const Stats &stats = result.print_statistics;

    json           rows = json::array();
    FilamentAmount total, support, flushed, tower;
    double         cost = 0.0;
    // Slots with filament use, as before the split columns were added.
    for (const auto &entry : stats.total_volumes_per_extruder) {
        const size_t         slot   = entry.first;
        const FilamentAmount used   = filament_from_volume(result, slot, entry.second);
        const FilamentAmount model  = filament_from_volume(result, slot, volume_of(stats.model_volumes_per_extruder, slot));
        const FilamentAmount sup    = filament_from_volume(result, slot, volume_of(stats.support_volumes_per_extruder, slot));
        const FilamentAmount flush  = filament_from_volume(result, slot, volume_of(stats.flush_per_filament, slot));
        const FilamentAmount wipe   = filament_from_volume(result, slot, volume_of(stats.wipe_tower_volumes_per_extruder, slot));
        const double         price  = slot < result.filament_costs.size() ? result.filament_costs[slot] : 0.0;
        const double         slot_cost = used.g * price / 1000.0; // filament_cost is per kg
        total.g += used.g;   total.m += used.m;
        support.g += sup.g;  support.m += sup.m;
        flushed.g += flush.g; flushed.m += flush.m;
        tower.g += wipe.g;   tower.m += wipe.m;
        cost += slot_cost;
        rows.push_back({{"slot", slot + 1},
                        {"used_g", rounded(used.g)},
                        {"used_m", rounded(used.m)},
                        {"model_g", rounded(model.g)},
                        {"support_g", rounded(sup.g)},
                        {"flushed_g", rounded(flush.g)},
                        {"tower_g", rounded(wipe.g)},
                        {"total_g", rounded(model.g + sup.g + flush.g + wipe.g)},
                        {"cost", rounded(slot_cost)}});
    }
    out["total_g"]   = rounded(total.g);
    out["total_m"]   = rounded(total.m);
    out["model_g"]   = rounded(total.g - support.g - flushed.g - tower.g);
    out["model_m"]   = rounded(total.m - support.m - flushed.m - tower.m);
    out["support_g"] = rounded(support.g);
    out["flushed_g"] = rounded(flushed.g);
    out["tower_g"]   = rounded(tower.g);
    out["cost"]      = rounded(cost);
    out["filaments"] = std::move(rows);
}

// Line Type rows: time, share of total time (the panel divides by total incl. prepare) and filament.
json features_to_json(const GCodeProcessorResult &result, const Stats::Mode &mode)
{
    const auto &per_role = result.print_statistics.used_filaments_per_role;
    json        rows     = json::array();
    for (const auto &role : mode.roles_times) {
        // erNone holds travel and other non-extrusion time; the panel shows that only as Travel.
        if (role.second <= 0.f || role.first == erNone)
            continue;
        json row = {{"feature", ExtrusionEntity::role_to_string(role.first)},
                    {"time_s", rounded(role.second, 10.0)},
                    {"percent", mode.time > 0.f ? rounded(100.0 * role.second / mode.time) : 0.0}};
        // Stored as (meters, grams).
        const auto used = per_role.find(role.first);
        row["used_g"] = used == per_role.end() ? 0.0 : rounded(used->second.second);
        row["used_m"] = used == per_role.end() ? 0.0 : rounded(used->second.first);
        rows.push_back(std::move(row));
    }
    return rows;
}
} // namespace

json slice_result_to_json(const GCodeProcessorResult &result)
{
    const Stats::Mode &mode = result.print_statistics.modes[static_cast<size_t>(Stats::ETimeMode::Normal)];

    const auto   timelapse      = result.skippable_part_time.find(SkipType::stTimelapse);
    const double timelapse_time = timelapse == result.skippable_part_time.end() ? 0.0 : timelapse->second;

    json warnings = json::array();
    for (const auto &warning : result.warnings)
        warnings.push_back({{"level", warning.level}, {"message", warning.msg}, {"code", warning.error_code}});

    json out = {
        {"time_s", rounded(mode.time, 1.0)},
        {"prepare_time_s", rounded(mode.prepare_time, 1.0)},
        {"timelapse_time_s", rounded(timelapse_time, 1.0)},
        // The panel's "Model printing time".
        {"model_time_s", rounded(mode.time - mode.prepare_time - timelapse_time, 1.0)},
        {"first_layer_time_s", rounded(result.initial_layer_time, 10.0)},
        {"layers", mode.layers_times.size()},
        {"filament_changes", result.print_statistics.total_filament_changes},
        {"flush_filament_changes", result.print_statistics.total_flush_filament_changes},
        {"features", features_to_json(result, mode)},
        {"warnings", std::move(warnings)},
    };
    add_filaments(result, out);
    out.update(slice_move_stats_to_json(result, mode));

    if (result.toolpath_outside)
        out["toolpath_outside"] = true;
    if (result.gcode_check_result.error_code != 0)
        out["gcode_check_error_code"] = result.gcode_check_result.error_code;
    if (result.conflict_result)
        out["conflict"] = {{"object_a", result.conflict_result->_objName1}, {"object_b", result.conflict_result->_objName2}, {"height", result.conflict_result->_height}};
    return out;
}

}}} // namespace Slic3r::GUI::AgentBridge
