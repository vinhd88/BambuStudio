#ifndef slic3r_AgentBridgeSliceJobs_hpp_
#define slic3r_AgentBridgeSliceJobs_hpp_

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI { namespace AgentBridge {

// Listens for Studio's slice completion for as long as the bridge runs. UI thread.
void install_slice_tracking();
void remove_slice_tracking();

// Tool: starts slicing a plate and returns a job id right away. Confirms the slice really started,
// because Studio's slice call can silently decline.
nlohmann::json slice_plate(const nlohmann::json &args);
// Tool: state of a job: queued, slicing, done, failed or blocked, and the result when done.
nlohmann::json get_job(const nlohmann::json &args);

// Studio re-checks the on-screen plate about 500 ms after a settings change, so a plate can still read
// as sliced straight after one. Set by apply_settings; cleared by the next finished slice.
void note_settings_changed();
bool settings_changed_since_last_slice();

}}} // namespace Slic3r::GUI::AgentBridge

#endif
