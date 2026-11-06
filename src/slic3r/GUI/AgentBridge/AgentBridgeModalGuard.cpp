#include "AgentBridgeModalGuard.hpp"

#include <algorithm>
#include <atomic>
#include <iterator>
#include <utility>
#include <memory>
#include <mutex>
#include <vector>

#include <wx/app.h>
#include <wx/dialog.h>
#include <wx/evtloop.h>
#include <wx/modalhook.h>
#include <wx/toplevel.h>

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
// Dialogs open now, oldest first, with their titles. Tracked by pointer, not by a bare count: the
// hook can be installed or removed while a dialog is open (the Preferences page switches the bridge
// on and off), and a dialog the hook never saw open must not be counted when it closes.
std::mutex                                     s_open_mutex;
std::vector<std::pair<wxDialog *, std::string>> s_open;
std::atomic<int>                               s_open_count{0};

class ModalHook : public wxModalDialogHook
{
protected:
    int Enter(wxDialog *dialog) override
    {
        std::lock_guard<std::mutex> lock(s_open_mutex);
        s_open.emplace_back(dialog, dialog != nullptr ? dialog->GetTitle().ToUTF8().data() : "");
        s_open_count = static_cast<int>(s_open.size());
        return wxID_NONE; // Let the dialog show as usual.
    }

    void Exit(wxDialog *dialog) override
    {
        std::lock_guard<std::mutex> lock(s_open_mutex);
        const auto it = std::find_if(s_open.rbegin(), s_open.rend(), [dialog](const auto &entry) { return entry.first == dialog; });
        if (it != s_open.rend())
            s_open.erase(std::next(it).base());
        s_open_count = static_cast<int>(s_open.size());
    }
};

std::unique_ptr<ModalHook> s_hook;
} // namespace

void install_modal_guard()
{
    if (s_hook)
        return;
    s_hook = std::make_unique<ModalHook>();
    s_hook->Register();
}

void remove_modal_guard()
{
    if (!s_hook)
        return;
    s_hook->Unregister();
    s_hook.reset();
    // Closes it will not see must not leave stale entries for the next install.
    std::lock_guard<std::mutex> lock(s_open_mutex);
    s_open.clear();
    s_open_count = 0;
}

int open_dialog_count() { return s_open_count.load(); }

std::string open_dialog_title()
{
    std::lock_guard<std::mutex> lock(s_open_mutex);
    return s_open.empty() ? std::string() : s_open.back().second;
}

bool modal_dialog_open()
{
    if (open_dialog_count() > 0)
        return true;
    for (auto node = wxTopLevelWindows.GetFirst(); node != nullptr; node = node->GetNext())
        if (auto *dialog = dynamic_cast<wxDialog *>(node->GetData()); dialog != nullptr && dialog->IsModal())
            return true;
    return false;
}

bool ui_thread_is_mid_operation()
{
    const wxEventLoopBase *active = wxEventLoopBase::GetActive();
    return active != nullptr && (active->IsYielding() || active != wxTheApp->GetMainLoop());
}

}}} // namespace Slic3r::GUI::AgentBridge
