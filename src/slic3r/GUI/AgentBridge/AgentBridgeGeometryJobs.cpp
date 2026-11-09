#include "AgentBridgeGeometryJobs.hpp"

#include <algorithm>
#include <deque>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeApplyHandlers.hpp"
#include "AgentBridgeGeometryChange.hpp"
#include "AgentBridgeTypes.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
constexpr size_t MAX_JOBS = 50;

struct GeometryJob
{
    std::string id;
    std::string change_id;
    std::string kind;
    bool        done = false;
    json        objects;
};

std::deque<GeometryJob> s_jobs;
int                     s_next_job = 1;

GeometryJob *find_job(const std::string &id)
{
    const auto it = std::find_if(s_jobs.begin(), s_jobs.end(), [&id](const GeometryJob &job) { return job.id == id; });
    return it == s_jobs.end() ? nullptr : &*it;
}

// Studio runs one UI job at a time, and the gate refuses changes while one runs, so the newest
// tracked job is the only one that can still be running.
bool still_running(const GeometryJob &job) { return &job == &s_jobs.back() && wxGetApp().plater()->is_any_job_running(); }

void mark_done(GeometryJob &job)
{
    job.done    = true;
    job.objects = objects_summary();
    if (ChangeRecord *record = change_journal().find(job.change_id); record != nullptr && record->outcome.value("status", "") == "running") {
        record->outcome["status"]  = "applied";
        record->outcome["objects"] = job.objects;
    }
}
} // namespace

std::string track_geometry_job(const std::string &change_id, const std::string &kind)
{
    if (s_jobs.size() >= MAX_JOBS)
        s_jobs.pop_front();
    s_jobs.push_back({"g" + std::to_string(s_next_job++), change_id, kind});
    return s_jobs.back().id;
}

bool is_geometry_job_id(const std::string &id) { return !id.empty() && id[0] == 'g'; }

void settle_geometry_jobs()
{
    // Objects are being rebuilt while a project loads; settle on a later call.
    if (s_jobs.empty() || wxGetApp().plater()->is_any_job_running() || wxGetApp().plater()->is_loading_project())
        return;
    for (GeometryJob &job : s_jobs)
        if (!job.done)
            mark_done(job);
}

json geometry_job_status(const std::string &id)
{
    GeometryJob *job = find_job(id);
    if (job == nullptr)
        throw BridgeError(404, "not_found", "No job " + id + ". Only recent jobs in this Studio session are kept.");
    if (!job->done && !still_running(*job))
        settle_geometry_jobs();
    json out = {{"job_id", job->id}, {"kind", job->kind}, {"change_id", job->change_id}, {"state", job->done ? "done" : "running"}};
    if (job->done)
        out["objects"] = job->objects;
    return out;
}

}}} // namespace Slic3r::GUI::AgentBridge
