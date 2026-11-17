#ifndef slic3r_AgentBridgeOfficialFilamentSyncButton_hpp_
#define slic3r_AgentBridgeOfficialFilamentSyncButton_hpp_

class wxWindow;

namespace Slic3r { namespace GUI { namespace AgentBridge {

// The "Project Filaments" header's sync button: previews what the official Studio's filaments
// would change, asks, then applies it and says how it went. Works whether or not agent access is
// switched on. UI thread.
void sync_filaments_from_official_studio_clicked(wxWindow *parent);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
