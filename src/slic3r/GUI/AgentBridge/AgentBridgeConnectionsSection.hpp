#ifndef slic3r_AgentBridgeConnectionsSection_hpp_
#define slic3r_AgentBridgeConnectionsSection_hpp_

#include <functional>

#include <wx/string.h>

class wxWindow;
class wxSizer;
class Button;

// "Connect AI apps" in the AI Agents Preferences page: one row per AI app with its status and a
// Connect button. UI thread.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// Adds the section to `sizer` on `page`, with buttons from `make_button` so they match the page.
// Returns the function that re-reads every app's status.
std::function<void()> add_connections_section(wxWindow *page, wxSizer *sizer, int row_padding, int text_width,
                                              const std::function<Button *(const wxString &)> &make_button);

// After startup: if AI apps are set up for another copy of Studio or another data folder, offers to
// connect them to this one. Asks once per Studio copy and data folder when the user declines.
void check_ai_app_connections();

}}} // namespace Slic3r::GUI::AgentBridge

#endif
