#include "AgentBridgeOfficialFilamentSyncButton.hpp"

#include <boost/log/trivial.hpp>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeOfficialFilamentSyncRun.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
const wxString &title()
{
    static const wxString text = _L("Sync filaments from official Studio");
    return text;
}

wxString u8(const std::string &text) { return wxString::FromUTF8(text); }

// UTF-8 spelled out: a narrow literal is not converted as UTF-8 everywhere.
wxString arrow() { return wxString::FromUTF8("  \xE2\x86\x92  "); }

wxString describe(const json &side)
{
    return u8(side.value("preset", "")) + (side.value("colour", "").empty() ? wxString() : "  " + u8(side.value("colour", "")));
}

// The slots that change, one line each, then presets and printer notes.
wxString changes_text(const json &report)
{
    wxString slots;
    for (const json &slot : report["slots"]) {
        const std::string status = slot["status"];
        if (status == "same")
            continue;
        wxString line = wxString::Format("\n  %d: ", slot["slot"].get<int>());
        const wxString from = slot.contains("before") ? describe(slot["before"]) : _L("new slot");
        if (status == "removed")
            line += from + arrow() + _L("removed");
        else if (status == "kept")
            line += from + arrow() + _L("kept");
        else
            line += from + arrow() + (slot.contains("after") ? describe(slot["after"]) : _L("not added"));
        if (slot.contains("reason"))
            line += "\n       (" + u8(slot["reason"].get<std::string>()) + ")";
        slots += line;
    }
    wxString text;
    const json &count = report["slot_count"];
    if (!slots.empty())
        text += wxString::Format(_L("Slots (%d now, %d after):"), count["before"].get<int>(), count["after"].get<int>()) + slots;
    const json &presets = report["presets"];
    if (!presets["added"].empty() || !presets["updated"].empty())
        text += "\n\n" + wxString::Format(_L("Custom filament presets: %d new, %d updated (replaced ones are backed up first)."),
                                          (int) presets["added"].size(), (int) presets["updated"].size());
    if (!report["printer_matches"].get<bool>())
        text += "\n\n" + wxString::Format(_L("The official Studio is set to the printer %s, this one to %s. Filaments that do not fit this printer keep their slot's filament."),
                                          u8(report["official_printer"].get<std::string>()), u8(report["current_printer"].get<std::string>()));
    if (report["mixed_slots_left_out"].get<int>() > 0)
        text += "\n\n" + _L("Mixed-filament slots are left out.");
    return text;
}

bool nothing_to_do(const json &report)
{
    for (const json &slot : report["slots"])
        if (slot["status"] != "same")
            return false;
    return report["presets"]["added"].empty() && report["presets"]["updated"].empty();
}
} // namespace

void sync_filaments_from_official_studio_clicked(wxWindow *parent)
{
    try {
        const FilamentSyncOutcome preview = run_official_filament_sync(false);
        if (preview.report.contains("warning"))
            throw FilamentSyncRefused("unsaved_filament_edits", preview.report["warning"].get<std::string>());
        if (nothing_to_do(preview.report)) {
            MessageDialog(parent, _L("This project's filaments already match the official Bambu Studio."), title(), wxOK | wxICON_INFORMATION).ShowModal();
            return;
        }
        MessageDialog ask(parent, _L("Copy the filaments from the official Bambu Studio into this project?") + "\n\n" + changes_text(preview.report), title(),
                          wxYES_NO | wxICON_QUESTION);
        if (ask.ShowModal() != wxID_YES)
            return;

        const FilamentSyncOutcome done = run_official_filament_sync(true);
        BOOST_LOG_TRIVIAL(info) << "agent bridge: filament sync from official Studio: " << done.report.dump();
        bool all_applied = !done.report.contains("stopped");
        for (const json &slot : done.report["slots"])
            all_applied &= slot["status"] != "not_applied";
        if (all_applied) {
            if (Plater *plater = wxGetApp().plater())
                plater->get_notification_manager()->push_notification(
                    wxString::Format(_L("Filaments synced from the official Bambu Studio: %d slots."), (int) done.after.size()).ToUTF8().data());
        } else
            MessageDialog(parent, _L("Some filaments were not synced:") + "\n\n" + changes_text(done.report), title(), wxOK | wxICON_WARNING).ShowModal();
    } catch (const std::exception &e) {
        MessageDialog(parent, u8(e.what()), title(), wxOK | wxICON_WARNING).ShowModal();
    }
}

}}} // namespace Slic3r::GUI::AgentBridge
