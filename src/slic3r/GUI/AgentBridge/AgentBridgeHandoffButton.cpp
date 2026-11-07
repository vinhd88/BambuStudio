#include "AgentBridgeHandoffButton.hpp"

#include <boost/log/trivial.hpp>

#include "slic3r/GUI/BackgroundSlicingProcess.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeHandoff.hpp"
#include "AgentBridgeSliceJobs.hpp"
#include "AgentBridgeTypes.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
// UI thread only. A click is waiting for a slice to finish.
bool s_pending = false;
bool s_bound   = false;

void refresh_print_button()
{
    if (MainFrame *frame = wxGetApp().mainframe)
        frame->update_slice_print_status(MainFrame::eEventSliceUpdate, true, true);
}

void stop_waiting()
{
    s_pending = false;
    refresh_print_button();
}

void show_problem(const std::string &message)
{
    show_error(wxGetApp().mainframe, from_u8(message + "\n\n" + _u8L("Nothing was handed off to the official Bambu Studio.")));
}

// The same handoff as the agent tool. The tool's own texts address agents, so outcomes are reworded here.
void hand_off()
{
    try {
        const json result = open_in_official_studio(json::object());
        const std::string status = result.value("status", "");
        if (status == "rejected") {
            show_problem(_u8L("The plate still has no current slice. Slice it, then try again."));
            return;
        }
        std::string text = _u8L("Opening a copy in the official Bambu Studio. Start the print there.");
        if (result.contains("warnings"))
            for (const json &warning : result["warnings"])
                text += " " + warning.get<std::string>();
        if (Plater *plater = wxGetApp().plater())
            plater->get_notification_manager()->push_notification(text);
        BOOST_LOG_TRIVIAL(info) << "agent bridge: handoff button " << status;
    } catch (const std::exception &e) {
        show_problem(e.what());
    }
}

// Runs before Studio's own handler, so the plate is checked once Studio has taken the result in.
void on_slice_completed(SlicingProcessCompletedEvent &event)
{
    event.Skip();
    if (!s_pending)
        return;
    if (event.success()) {
        wxGetApp().CallAfter([] {
            stop_waiting();
            hand_off();
        });
    } else if (event.cancelled()) {
        // Studio also cancels a process it stops to restart. If slicing goes on, keep waiting.
        wxGetApp().CallAfter([] {
            Plater *plater = wxGetApp().plater();
            if (plater != nullptr && plater->is_background_process_slicing())
                return;
            stop_waiting();
            show_problem(_u8L("Slicing was cancelled."));
        });
    } else {
        const std::string reason = event.format_error_message().first;
        wxGetApp().CallAfter([reason] {
            stop_waiting();
            show_problem(_u8L("Slicing failed:") + " " + reason);
        });
    }
}

void wait_for_slice()
{
    if (!s_bound) {
        wxGetApp().plater()->Bind(EVT_PROCESS_COMPLETED, on_slice_completed);
        s_bound = true;
    }
    s_pending = true;
    refresh_print_button();
}
} // namespace

bool button_handoff_pending() { return s_pending; }

void open_in_official_studio_from_button()
{
    Plater *plater = wxGetApp().plater();
    if (s_pending || plater == nullptr)
        return;
    try {
        // A slice already running (e.g. started by an agent) is waited for, not restarted.
        if (plater->is_background_process_slicing()) {
            wait_for_slice();
            return;
        }
        PartPlate *plate = plater->get_partplate_list().get_curr_plate();
        if (plate != nullptr && plate->is_slice_result_valid() && !settings_changed_since_last_slice()) {
            hand_off();
            return;
        }
        // The slice_plate tool's steps: validates the plate, says why Studio declines, starts the slice.
        const json job   = slice_plate(json::object());
        const std::string state = job.value("state", "");
        if (state == "slicing")
            wait_for_slice();
        else if (state == "done")
            hand_off();
        else
            show_problem(job.value("reason", _u8L("Studio could not slice this plate.")));
    } catch (const std::exception &e) {
        s_pending = false;
        show_problem(e.what());
    }
}

}}} // namespace Slic3r::GUI::AgentBridge
