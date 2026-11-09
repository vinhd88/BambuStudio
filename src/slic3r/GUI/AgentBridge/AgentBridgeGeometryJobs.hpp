#ifndef slic3r_AgentBridgeGeometryJobs_hpp_
#define slic3r_AgentBridgeGeometryJobs_hpp_

#include <string>

#include <nlohmann/json.hpp>

// Tracks Studio UI jobs (arrange, orient) that an agent change started. They run on a worker thread
// and finish on the UI thread later, so the tool returns a job id and get_job reports the outcome.
// UI thread.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// Call right after starting the job inside the change. Returns the job id ("g<N>").
std::string track_geometry_job(const std::string &change_id, const std::string &kind);

bool is_geometry_job_id(const std::string &id);

// Records the outcome of every tracked job that has finished, so get_studio_state's last_change shows it
// even when nobody polls get_job.
void settle_geometry_jobs();

// get_job for a geometry job: state running or done; when done, the object list afterwards.
nlohmann::json geometry_job_status(const std::string &id);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
