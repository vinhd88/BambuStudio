#include "AgentBridgeSliceJobs.hpp"

#include <algorithm>
#include <deque>

#include <boost/log/trivial.hpp>

#include "slic3r/GUI/BackgroundSlicingProcess.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Event.hpp"

#include "AgentBridgeArgs.hpp"
#include "AgentBridgeMutationGate.hpp"
#include "AgentBridgeProjectJson.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
constexpr size_t MAX_JOBS = 32;

struct SliceJob
{
    std::string id;
    int         plate = 0;
    std::string state = "queued";
    std::string reason;
    bool        already_sliced = false;
};

std::deque<SliceJob> s_jobs;
int                  s_next_job = 1;
bool                 s_settings_changed = false;
bool                 s_tracking = false;

SliceJob *find_job(const std::string &id)
{
    const auto it = std::find_if(s_jobs.begin(), s_jobs.end(), [&id](const SliceJob &job) { return job.id == id; });
    return it == s_jobs.end() ? nullptr : &*it;
}

SliceJob *slicing_job()
{
    const auto it = std::find_if(s_jobs.begin(), s_jobs.end(), [](const SliceJob &job) { return job.state == "slicing"; });
    return it == s_jobs.end() ? nullptr : &*it;
}

// Runs before Studio's own handler, so the plate's result is not updated yet. get_job reads it later.
void on_slice_completed(SlicingProcessCompletedEvent &event)
{
    event.Skip();
    s_settings_changed = false;
    SliceJob *job = slicing_job();
    if (job == nullptr)
        return;
    if (event.success()) {
        job->state = "done";
    } else if (event.cancelled()) {
        job->state  = "failed";
        job->reason = "Slicing was cancelled.";
        // Studio also posts a cancel for a process it stopped to restart, which can arrive after the
        // restarted slice began. If slicing goes on once Studio has handled the event, the job does too.
        wxGetApp().CallAfter([id = job->id] {
            SliceJob *same = find_job(id);
            if (same != nullptr && same->state == "failed" && wxGetApp().plater() != nullptr && wxGetApp().plater()->is_background_process_slicing()) {
                same->state = "slicing";
                same->reason.clear();
            }
        });
    } else {
        job->state  = "failed";
        job->reason = event.format_error_message().first;
    }
    BOOST_LOG_TRIVIAL(info) << "agent bridge: " << job->id << " " << job->state << " " << job->reason;
}

// Why Studio will decline to slice, for the cases it does not say so itself. Same order as Plater::reslice.
std::string decline_reason(Plater &plater)
{
    if (plater.sidebar().has_broken_mixed_filament())
        return "A mixed filament on this plate is broken. The user needs to fix it in Studio.";
    if (plater.sidebar().has_incompatible_filament())
        return "This plate uses a filament flagged as incompatible. The user needs to fix it in Studio.";
    if (plater.get_view3D_canvas3D()->get_gizmos_manager().is_in_editing_mode(true))
        return "A tool with unsaved edits is open in Studio. The user needs to close it.";
    return "Studio declined to slice. Its notifications should say why.";
}

// Same steps as the Slice button, minus the prompts tied to dual-extruder printers.
void start_slice(Plater &plater, SliceJob &job)
{
    PartPlateList &plates = plater.get_partplate_list();
    if (job.plate != plates.get_curr_plate_index())
        plater.select_plate(job.plate);

    PartPlate &plate = *plates.get_curr_plate();
    if (plate.empty()) {
        job.state  = "blocked";
        job.reason = "This plate has nothing on it.";
        return;
    }
    if (plater.is_background_process_update_scheduled())
        plater.update(false, true);
    plater.reset_check_status();
    plater.set_slice_from_slice_btn(true);
    plater.update(true, true);

    bool model_fits = false, validate_error = false;
    plater.validate_current_plate(model_fits, validate_error);
    if (!model_fits || validate_error) {
        job.state  = "blocked";
        job.reason = !model_fits ? "An object is outside the plate or the filament cannot be printed on it. See Studio's notifications."
                                 : "These settings do not validate for slicing. See Studio's notifications.";
        return;
    }

    // Synchronous, unlike the button's wxPostEvent, so the outcome is known on return.
    SimpleEvent slice_event(EVT_GLTOOLBAR_SLICE_PLATE);
    plater.GetEventHandler()->ProcessEvent(slice_event);

    if (plater.is_background_process_slicing()) {
        job.state = "slicing";
        wxGetApp().mainframe->select_tab(MainFrame::tpPreview);
    } else if (plate.is_slice_result_valid()) {
        job.state          = "done";
        job.already_sliced = true;
        s_settings_changed = false; // Studio found the result still matches the settings.
    } else {
        job.state  = "blocked";
        job.reason = decline_reason(plater);
    }
}
} // namespace

void install_slice_tracking()
{
    if (s_tracking || wxGetApp().plater() == nullptr)
        return;
    wxGetApp().plater()->Bind(EVT_PROCESS_COMPLETED, on_slice_completed);
    s_tracking = true;
}

void remove_slice_tracking()
{
    if (!s_tracking)
        return;
    if (Plater *plater = wxGetApp().plater())
        plater->Unbind(EVT_PROCESS_COMPLETED, on_slice_completed);
    s_tracking = false;
}

json slice_plate(const json &args)
{
    Plater        &plater = *wxGetApp().plater();
    PartPlateList &plates = plater.get_partplate_list();

    int index = optional_index_arg(args, "plate", plates.get_plate_count());
    if (index < 0)
        index = plates.get_curr_plate_index();

    // Only one slice runs at a time. A job still marked slicing lost its completion event; leaving it
    // would let it claim the next completion.
    for (SliceJob &old : s_jobs)
        if (old.state == "slicing") {
            old.state  = "failed";
            old.reason = "Superseded by a later slice.";
        }

    SliceJob job;
    job.id    = "j" + std::to_string(s_next_job++);
    job.plate = index;
    if (s_jobs.size() >= MAX_JOBS)
        s_jobs.pop_front();
    s_jobs.push_back(job);
    const std::string id = job.id;

    {
        MutationScope running("job_id", id);
        // A dialog raised by Studio here keeps this call open; get_job shows the job as queued meanwhile.
        start_slice(plater, s_jobs.back());
    }
    BOOST_LOG_TRIVIAL(info) << "agent bridge: " << id << " plate " << index + 1 << " " << s_jobs.back().state;
    return get_job({{"job_id", id}});
}

json get_job(const json &args)
{
    const std::string id = string_arg(args, "job_id");
    if (id.empty())
        throw BridgeError(400, "invalid_args", "job_id is required");
    SliceJob *job = find_job(id);
    if (job == nullptr)
        throw BridgeError(404, "not_found", "No job " + id + ". Only recent jobs in this Studio session are kept.");

    Plater        &plater = *wxGetApp().plater();
    PartPlateList &plates = plater.get_partplate_list();

    // A safety net: the completion event should have come by now.
    if (job->state == "slicing" && !plater.is_background_process_slicing()) {
        job->state  = "failed";
        job->reason = "Slicing stopped without reporting a result.";
    }

    json out = {{"job_id", job->id}, {"plate", job->plate + 1}, {"state", job->state}};
    if (!job->reason.empty())
        out["reason"] = job->reason;
    if (job->already_sliced)
        out["already_sliced"] = true;

    // get_job answers with a dialog open too, but plate results are being rebuilt while a project loads.
    if (job->plate >= plates.get_plate_count() || plater.is_loading_project())
        return out;
    PartPlate &plate = *plates.get_plate(job->plate);
    if (job->state == "slicing") {
        out["progress_percent"] = plate.get_slicing_percent();
    } else if (job->state == "done") {
        const GCodeProcessorResult *result = plate.get_slice_result();
        if (plate.is_slice_result_valid() && result != nullptr)
            out["result"] = slice_result_to_json(*result);
        else
            out["result_outdated"] = true; // Settings changed since, or the plate changed.
    }
    return out;
}

void note_settings_changed() { s_settings_changed = true; }

bool settings_changed_since_last_slice() { return s_settings_changed; }

}}} // namespace Slic3r::GUI::AgentBridge
