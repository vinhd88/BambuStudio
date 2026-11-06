#ifndef slic3r_AgentBridge_hpp_
#define slic3r_AgentBridge_hpp_

// Entry points for the agent bridge, called from the GUI app lifecycle.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// Starts serving if the switch is on and Studio runs with a separate data dir. Never throws: a bridge failure must not
// stop Studio from starting.
void start_bridge();

// Stops serving and removes the discovery file. Must run before Studio state is torn down.
void stop_bridge();

// The agent access switch, saved in the app config; on unless the user turned it off.
bool bridge_enabled();
// UI thread only, as are start/stop: they join the bridge's io thread.
// Saves the switch and starts or stops the bridge now. Off: no listening socket, no discovery file.
void set_bridge_enabled(bool enabled);
// Restarts a running bridge with a new token (and port), so the old token stops working.
void reset_bridge_token();

}}} // namespace Slic3r::GUI::AgentBridge

#endif
