#include "AgentBridge.hpp"

#include <chrono>
#include <memory>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <wx/stdpaths.h>

#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include "AgentBridgeReadHandlers.hpp"
#include "AgentBridgeServer.hpp"
#include "AgentBridgeUiThreadCall.hpp"

namespace fs = boost::filesystem;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
constexpr std::chrono::seconds UI_CALL_TIMEOUT(3);
const char *const              DISCOVERY_FILE_NAME = "agent-bridge.json";

std::unique_ptr<AgentBridgeServer> s_server;

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
} // namespace

void start_bridge()
{
    if (s_server)
        return;
    try {
        if (uses_default_data_dir()) {
            BOOST_LOG_TRIVIAL(warning) << "agent bridge: not started, Studio is using the default data folder. Pass --datadir to enable it.";
            return;
        }

        const std::string discovery_path = (fs::path(data_dir()) / DISCOVERY_FILE_NAME).string();
        // The handler is only called while s_server is set: stop_bridge joins the io thread first.
        auto server = std::make_unique<AgentBridgeServer>(discovery_path, [](const std::string &tool, const std::string &body, std::function<void(BridgeResponse)> done) {
            call_on_ui_thread(s_server->io(), post_to_ui_thread, UI_CALL_TIMEOUT,
                              [tool, body] { return handle_tool_call(tool, body); }, std::move(done));
        });
        s_server = std::move(server);
        s_server->start();
        BOOST_LOG_TRIVIAL(info) << "agent bridge: listening on 127.0.0.1:" << s_server->port();
    } catch (const std::exception &e) {
        s_server.reset();
        BOOST_LOG_TRIVIAL(error) << "agent bridge: failed to start: " << e.what();
    }
}

void stop_bridge()
{
    if (!s_server)
        return;
    try {
        s_server->stop();
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "agent bridge: failed to stop cleanly: " << e.what();
    }
    s_server.reset();
}

}}} // namespace Slic3r::GUI::AgentBridge
