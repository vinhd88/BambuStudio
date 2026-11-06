#include "AgentBridgeModalGuard.hpp"

#include <atomic>
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
std::mutex               s_titles_mutex;
std::vector<std::string> s_titles;
std::atomic<int>         s_open_count{0};

class ModalHook : public wxModalDialogHook
{
protected:
    int Enter(wxDialog *dialog) override
    {
        {
            std::lock_guard<std::mutex> lock(s_titles_mutex);
            s_titles.push_back(dialog != nullptr ? dialog->GetTitle().ToUTF8().data() : "");
        }
        ++s_open_count;
        return wxID_NONE; // Let the dialog show as usual.
    }

    void Exit(wxDialog *) override
    {
        --s_open_count;
        std::lock_guard<std::mutex> lock(s_titles_mutex);
        if (!s_titles.empty())
            s_titles.pop_back();
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
}

int open_dialog_count() { return s_open_count.load(); }

std::string open_dialog_title()
{
    std::lock_guard<std::mutex> lock(s_titles_mutex);
    return s_titles.empty() ? std::string() : s_titles.back();
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
