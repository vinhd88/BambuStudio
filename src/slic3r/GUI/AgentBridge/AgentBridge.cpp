#include "AgentBridge.hpp"

#include <chrono>
#include <memory>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <wx/stdpaths.h>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"

#include "AgentBridgeAgentDataFolder.hpp"

#include "AgentBridgeDiscoveryFile.hpp"
#include "AgentBridgeModalGuard.hpp"
#include "AgentBridgeMutationGate.hpp"
#include "AgentBridgeServer.hpp"
#include "AgentBridgeSliceJobs.hpp"
#include "AgentBridgeToolDispatch.hpp"
#include "AgentBridgeUiThreadCall.hpp"

namespace fs = boost::filesystem;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
constexpr std::chrono::seconds UI_CALL_TIMEOUT(3);
const char *const              DISCOVERY_FILE_NAME = "agent-bridge.json";
// App config switch for agent access. Missing means on: the agent build exists for this.
const char *const              ENABLED_KEY         = "agent_bridge_enabled";

std::unique_ptr<AgentBridgeServer> s_server;
// Why the switch is on but nothing listens, for the Preferences tab. Empty when running or off.
std::string                        s_start_problem;

// The custom build must not write to the official app's data folder, even a small token file.
bool uses_default_data_dir()
{
    boost::system::error_code ec;
    const fs::path default_dir = wxStandardPaths::Get().GetUserDataDir().ToUTF8().data();
    const bool     same        = fs::equivalent(fs::path(data_dir()), default_dir, ec);
    return ec || same;
}

void post_to_ui_thread(std::function<void()> fn)
{
    // A call still queued when the bridge stops must not run: nobody can receive its reply, and the
    // timeout that would have cancelled it died with the io thread. s_server only changes on this thread.
    wxGetApp().CallAfter([fn = std::move(fn)] {
        if (s_server)
            fn();
    });
}
// A native macOS dialog (file picker, alert) runs its own loop that does not run queued calls, so
// every call times out as studio_busy while it is open. The modal hook still counts it, so the client
// can be told what Studio is waiting for. The call never ran, so nothing changed.
std::function<void(BridgeResponse)> name_open_dialog(std::function<void(BridgeResponse)> done)
{
    return [done = std::move(done)](BridgeResponse response) {
        if (!response.work_ran && open_dialog_count() > 0) {
            const std::string title = open_dialog_title();
            response = error_response(409, "waiting_for_user",
                                      (title.empty() ? std::string("A dialog") : "The dialog \"" + title + "\"") +
                                          " is open in Studio. Nothing was changed. Ask the user to close it, then try again.");
        }
        done(std::move(response));
    };
}
} // namespace

BridgeStatus bridge_status()
{
    BridgeStatus status;
    status.enabled = bridge_enabled();
    status.running = s_server != nullptr;
    status.port    = s_server ? s_server->port() : 0;
    status.problem = s_start_problem;
    return status;
}

bool bridge_enabled() { return wxGetApp().app_config->get(ENABLED_KEY) != "false"; }

void set_bridge_enabled(bool enabled)
{
    wxGetApp().app_config->set(ENABLED_KEY, enabled ? "true" : "false");
    // Saved now: turning access off must hold even if Studio crashes before its next save.
    wxGetApp().app_config->save();
    if (enabled)
        start_bridge();
    else
        stop_bridge();
}

void reset_bridge_token()
{
    // A new server makes a new token and port and rewrites the discovery file. The --mcp-stdio
    // helper reads that file on every call, so connected AI apps carry on; anything holding the
    // old token is locked out.
    if (!s_server)
        return;
    stop_bridge();
    start_bridge();
}

void start_bridge()
{
    if (s_server)
        return;
    s_start_problem.clear();
    if (!bridge_enabled()) {
        // Off means no discovery file either, even one a crash left behind.
        if (!uses_default_data_dir())
            remove_stale_discovery_file((fs::path(data_dir()) / DISCOVERY_FILE_NAME).string());
        BOOST_LOG_TRIVIAL(info) << "agent bridge: off in settings";
        return;
    }
    try {
        if (uses_default_data_dir()) {
            s_start_problem = "Studio is using the official app's data folder, which agents must not touch. Start it with --datadir.";
            BOOST_LOG_TRIVIAL(warning) << "agent bridge: not started, Studio is using the default data folder. Pass --datadir to enable it.";
            return;
        }

        const std::string discovery_path = (fs::path(data_dir()) / DISCOVERY_FILE_NAME).string();
        // The handler is only called while s_server is set: stop_bridge joins the io thread first.
        auto server = std::make_unique<AgentBridgeServer>(discovery_path, [](const std::string &tool, const std::string &body, std::function<void(BridgeResponse)> done) {
            call_on_ui_thread(s_server->io(), post_to_ui_thread, UI_CALL_TIMEOUT,
                              [tool, body] { return handle_tool_call(tool, body); }, name_open_dialog(std::move(done)),
                              // Only a change or slice start is answered before it ends; a slow read just takes longer.
                              is_mutation_tool(tool) ? std::function<std::optional<BridgeResponse>()>(overrun_response) : nullptr);
        });
        s_server = std::move(server);
        s_server->start();
        install_modal_guard();
        install_slice_tracking();
        BOOST_LOG_TRIVIAL(info) << "agent bridge: listening on 127.0.0.1:" << s_server->port();
    } catch (const std::exception &e) {
        s_server.reset();
        s_start_problem = std::string("Could not start: ") + e.what();
        BOOST_LOG_TRIVIAL(error) << "agent bridge: failed to start: " << e.what();
    }
}

void stop_bridge()
{
    s_start_problem.clear();
    if (!s_server)
        return;
    remove_slice_tracking();
    remove_modal_guard();
    try {
        s_server->stop();
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "agent bridge: failed to stop cleanly: " << e.what();
    }
    s_server.reset();
}

namespace {
std::string import_notice_text(const ImportSummary &summary)
{
    if (!summary.source_found)
        return _u8L("No official Bambu Studio data was found, so this build starts without your printers and presets.");
    std::string text;
    try {
        text = summary.user_presets > 0 || summary.config_copied
                           ? (boost::format(_u8L("Copied from the official Bambu Studio: %1% user presets, printer profiles and preset selections. "
                                                 "Account, login, access codes and printer addresses were not copied."))
                              % summary.user_presets).str()
                           : _u8L("Nothing to copy was found in the official Bambu Studio data folder.");
    } catch (const std::exception &) {
        // A translation that breaks the placeholder must not stop Studio from starting.
        text = "Copied " + std::to_string(summary.user_presets) + " user presets from the official Bambu Studio.";
    }
    if (!summary.backup.empty())
        text += " " + _u8L("Your previous presets were saved to:") + " " + summary.backup;
    if (!summary.problems.empty())
        text += " " + _u8L("Not copied:") + " " + summary.problems.front() + (summary.problems.size() > 1 ? " (+" + std::to_string(summary.problems.size() - 1) + ")" : "");
    return text;
}
} // namespace

void show_startup_import_notice()
{
    const auto summary = take_startup_import_summary();
    if (!summary)
        return;
    for (const std::string &problem : summary->problems)
        BOOST_LOG_TRIVIAL(warning) << "agent data import: " << problem;
    BOOST_LOG_TRIVIAL(info) << "agent data import: source found " << summary->source_found << ", " << summary->user_presets << " preset files, config "
                            << summary->config_copied;
    const std::string text  = import_notice_text(*summary);
    const bool        clean = summary->problems.empty();
    // A dialog, not a 3D-view notification: with no project open Studio starts on the Home page,
    // where notifications are not drawn, and they fade before the user switches. Shown once.
    wxGetApp().CallAfter([text, clean] {
        MessageDialog(wxGetApp().mainframe, wxString::FromUTF8(text), _L("Agent build data folder"), wxOK | (clean ? wxICON_INFORMATION : wxICON_WARNING))
            .ShowModal();
    });
}

void request_reimport()
{
    if (uses_default_data_dir())
        throw std::runtime_error(_u8L("Studio is using the official app's data folder, so there is nothing to import into."));
    schedule_reimport(data_dir());
}

}}} // namespace Slic3r::GUI::AgentBridge
