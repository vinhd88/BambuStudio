#ifndef slic3r_AgentBridge_hpp_
#define slic3r_AgentBridge_hpp_

#include <cstdint>
#include <string>

// Entry points for the agent bridge, called from the GUI app lifecycle and the Preferences tab.
namespace Slic3r { namespace GUI { namespace AgentBridge {

struct BridgeStatus
{
    bool        enabled = false; // the switch
    bool        running = false; // listening now
    uint16_t    port    = 0;
    std::string problem;         // why it is on but not running; empty otherwise
};

// UI thread.
BridgeStatus bridge_status();

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

// After startup: a dialog, once the main window is up, telling the user what the first-run import or a
// requested re-import copied from the official Studio, if one ran.
void show_startup_import_notice();
// Marks a re-import from the official Studio for the next start, which first backs up this build's
// presets. Throws with a message for the user if that is not possible.
void request_reimport();

}}} // namespace Slic3r::GUI::AgentBridge

#endif
