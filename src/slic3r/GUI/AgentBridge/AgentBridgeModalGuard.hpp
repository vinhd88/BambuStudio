#ifndef slic3r_AgentBridgeModalGuard_hpp_
#define slic3r_AgentBridgeModalGuard_hpp_

#include <string>

namespace Slic3r { namespace GUI { namespace AgentBridge {

// Tracks open modal dialogs through wxModalDialogHook. wxWidgets calls the hook from ShowModal of
// every dialog class, including the macOS native message, file and folder dialogs, which do not show
// up as modal wxDialog windows.
void install_modal_guard();
void remove_modal_guard();

// Thread safe, so the io thread can report a dialog while the UI thread sits inside it.
int         open_dialog_count();
// Title of the innermost open dialog. Many Studio dialogs have an empty one.
std::string open_dialog_title();

// UI thread only. Also catches a dialog opened before the guard was installed.
bool modal_dialog_open();

// UI thread only. wxWidgets also runs queued calls while Studio is partway through a long operation:
// inside a nested event loop, or when ProgressDialog yields during an import or project load. The UI
// thread is then parked mid-operation, so Studio's state is not consistent. A progress dialog is not
// modal while it yields, so modal_dialog_open alone misses this.
bool ui_thread_is_mid_operation();

}}} // namespace Slic3r::GUI::AgentBridge

#endif
