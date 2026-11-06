#ifndef slic3r_AgentBridgePreferencesTab_hpp_
#define slic3r_AgentBridgePreferencesTab_hpp_

class wxWindow;

// The "AI Agents" page of Preferences: agent access switch, status, and access token reset.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// Builds the page as a child of `parent` (the Preferences book). UI thread.
wxWindow *create_preferences_tab(wxWindow *parent);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
