#include "AgentBridgeMutationGate.hpp"

#include <mutex>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeModalGuard.hpp"

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
std::mutex  s_running_mutex;
std::string s_id_field;
std::string s_id;

std::string dialog_description()
{
    const std::string title = open_dialog_title();
    return title.empty() ? "A dialog" : "The dialog \"" + title + "\"";
}
} // namespace

void require_can_mutate()
{
    Plater &plater = *wxGetApp().plater();
    if (modal_dialog_open())
        throw BridgeError(409, "waiting_for_user", dialog_description() + " is open in Studio. Nothing was changed. Ask the user to close it, then try again.");
    if (!running_mutation_id().empty())
        throw BridgeError(409, "waiting_for_user", "Change or handoff " + running_mutation_id() + " has not finished. Check get_studio_state.");
    if (ui_thread_is_mid_operation() || plater.is_loading_project())
        throw BridgeError(503, "studio_busy", "Studio is partway through an operation. Nothing was changed. Try again shortly.");
    // Studio would cancel the slice to apply a change, which a client that started it would not expect.
    if (plater.is_background_process_slicing())
        throw BridgeError(409, "slicing_in_progress", "Studio is slicing. Nothing was changed. Wait for it to finish; get_job reports progress.");
    if (plater.only_gcode_mode() || plater.is_gcode_3mf())
        throw BridgeError(409, "gcode_project", "The open file is sliced G-code, which has no settings to change or slice.");
}

MutationScope::MutationScope(const std::string &id_field, const std::string &id)
{
    std::lock_guard<std::mutex> lock(s_running_mutex);
    s_id_field = id_field;
    s_id       = id;
}

MutationScope::~MutationScope()
{
    std::lock_guard<std::mutex> lock(s_running_mutex);
    s_id_field.clear();
    s_id.clear();
}

std::string running_mutation_id()
{
    std::lock_guard<std::mutex> lock(s_running_mutex);
    return s_id;
}

std::optional<BridgeResponse> overrun_response()
{
    std::string id_field, id;
    {
        std::lock_guard<std::mutex> lock(s_running_mutex);
        id_field = s_id_field;
        id       = s_id;
    }
    if (id.empty())
        return std::nullopt;

    nlohmann::json result = {{id_field, id}};
    if (open_dialog_count() > 0) {
        result["status"]  = "waiting_for_user";
        result["dialog"]  = open_dialog_title();
        result["message"] = dialog_description() + " opened in Studio and needs the user's answer. The outcome appears in get_studio_state once it is closed.";
    } else {
        result["status"]  = "in_progress";
        result["message"] = "Still running. The outcome appears in get_studio_state.";
    }
    return ok_response(result);
}

}}} // namespace Slic3r::GUI::AgentBridge
