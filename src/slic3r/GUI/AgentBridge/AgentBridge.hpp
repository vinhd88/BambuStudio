#ifndef slic3r_AgentBridge_hpp_
#define slic3r_AgentBridge_hpp_

// Entry points for the agent bridge, called from the GUI app lifecycle.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// Starts serving if Studio runs with a separate data dir. Never throws: a bridge failure must not
// stop Studio from starting.
void start_bridge();

// Stops serving and removes the discovery file. Must run before Studio state is torn down.
void stop_bridge();

}}} // namespace Slic3r::GUI::AgentBridge

#endif
