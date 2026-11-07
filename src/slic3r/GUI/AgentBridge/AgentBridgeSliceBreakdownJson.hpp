#ifndef slic3r_AgentBridgeSliceBreakdownJson_hpp_
#define slic3r_AgentBridgeSliceBreakdownJson_hpp_

#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "libslic3r/GCode/GCodeProcessor.hpp"

// Opt-in detail for get_slice_result: per-layer rows and per-object time and filament, built from
// the slice result's moves. No GUI dependency; the handler supplies object names.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// Most layer rows returned in one call; more sets layers_truncated.
constexpr int MAX_LAYER_ROWS = 2000;

// What get_slice_result's include, layer_from and layer_to ask for. Layers count from 1; 0 means open.
struct BreakdownRequest
{
    bool layers     = false;
    bool objects    = false;
    int  layer_from = 0;
    int  layer_to   = 0;
};

// Reads include / layer_from / layer_to. Throws BridgeError invalid_args on bad input.
BreakdownRequest parse_breakdown_request(const nlohmann::json &args);

// Printed object instance a G-code label id belongs to (numbers from 1, as in get_project).
struct ObjectLabel
{
    int         object   = 0;
    int         instance = 0;
    std::string name;
};
using ObjectLabels = std::map<int, ObjectLabel>;

// {"layers_detail": [{layer, z_mm, height_mm, time_s, used_g, slots}], "layer_time_source"?, "layers_truncated"?}
nlohmann::json slice_layers_to_json(const GCodeProcessorResult &result, int layer_from, int layer_to);

// {"objects_detail": [...], "objects_note"} or {"objects_unavailable": reason} when label objects is off.
nlohmann::json slice_objects_to_json(const GCodeProcessorResult &result, const ObjectLabels &labels);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
