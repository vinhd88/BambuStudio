#include "AgentBridgeToolDispatch.hpp"

#include <functional>
#include <unordered_map>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeApplyHandlers.hpp"
#include "AgentBridgeFilamentHandlers.hpp"
#include "AgentBridgeHandoff.hpp"
#include "AgentBridgeModalGuard.hpp"
#include "AgentBridgeMutationGate.hpp"
#include "AgentBridgePresetSelection.hpp"
#include "AgentBridgeReadHandlers.hpp"
#include "AgentBridgeRevertHandler.hpp"
#include "AgentBridgeSliceJobs.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
enum class Gate {
    None,    // Answers even with a dialog open, so a client can find out what Studio is waiting for.
    Read,    // Needs consistent state.
    Mutate,  // Changes the project.
};

struct Tool
{
    std::function<json(const json &)> run;
    Gate                              gate;
};

const std::unordered_map<std::string, Tool> &tools()
{
    static const std::unordered_map<std::string, Tool> table = {
        {"get_studio_state", {get_studio_state, Gate::None}},
        {"get_job", {get_job, Gate::None}},
        {"get_project", {get_project, Gate::Read}},
        {"get_settings", {get_settings, Gate::Read}},
        {"describe_settings", {describe_settings, Gate::Read}},
        {"list_presets", {list_presets, Gate::Read}},
        {"list_filaments", {list_filaments, Gate::Read}},
        {"get_filament", {get_filament, Gate::Read}},
        {"get_slice_result", {get_slice_result, Gate::Read}},
        {"apply_settings", {apply_settings, Gate::Mutate}},
        {"slice_plate", {slice_plate, Gate::Mutate}},
        {"select_filament", {select_filament, Gate::Mutate}},
        {"select_process_preset", {select_process_preset, Gate::Mutate}},
        {"set_object_filament", {set_object_filament, Gate::Mutate}},
        {"revert_change", {revert_change, Gate::Mutate}},
        // Changes nothing, but saves a copy, which needs a quiet Studio just as a change does.
        {"open_in_official_studio", {open_in_official_studio, Gate::Mutate}},
    };
    return table;
}

void require_can_read(GUI_App &app)
{
    if (modal_dialog_open())
        throw BridgeError(503, "studio_busy", "A dialog is open in Studio. Close it, then try again.");
    if (ui_thread_is_mid_operation())
        throw BridgeError(503, "studio_busy", "Studio is partway through an operation. Try again shortly.");
    if (app.plater()->is_loading_project())
        throw BridgeError(503, "studio_busy", "Studio is loading a project. Try again shortly.");
}

json parse_args(const std::string &body)
{
    if (body.empty())
        return json::object();
    json args = json::parse(body, nullptr, false);
    if (args.is_discarded() || !args.is_object())
        throw BridgeError(400, "invalid_args", "Request body must be a JSON object");
    return args;
}
} // namespace

bool is_mutation_tool(const std::string &tool)
{
    const auto it = tools().find(tool);
    return it != tools().end() && it->second.gate == Gate::Mutate;
}

BridgeResponse handle_tool_call(const std::string &tool, const std::string &body)
{
    const auto it = tools().find(tool);
    if (it == tools().end())
        throw BridgeError(404, "unknown_tool", "Unknown tool: " + tool);

    // plater_ is not cleared at shutdown, so these checks stand between a late call and a dangling pointer.
    GUI_App &app = wxGetApp();
    if (!app.initialized() || app.is_closing() || app.is_recreating_gui() || app.mainframe == nullptr || app.plater() == nullptr || app.preset_bundle == nullptr)
        throw BridgeError(503, "studio_unavailable", "Studio is starting up or shutting down");

    if (it->second.gate == Gate::Read)
        require_can_read(app);
    else if (it->second.gate == Gate::Mutate)
        require_can_mutate();
    return ok_response(it->second.run(parse_args(body)));
}

}}} // namespace Slic3r::GUI::AgentBridge
