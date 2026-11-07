#ifndef slic3r_AgentBridgeHandoffButton_hpp_
#define slic3r_AgentBridgeHandoffButton_hpp_

namespace Slic3r { namespace GUI { namespace AgentBridge {

// The Print dropdown's "Open in official Studio": the same handoff as the open_in_official_studio
// tool, in one click. Slices the current plate first when it has no current slice, waits for the
// slice to finish, then saves the copy and opens it in the official app. Problems are shown in a
// dialog. Works whether or not agent access is switched on. UI thread.
void open_in_official_studio_from_button();

// True while a click is waiting for its slice to finish, so the button can stay disabled.
bool button_handoff_pending();

}}} // namespace Slic3r::GUI::AgentBridge

#endif
