#ifndef slic3r_AgentBridgeUiThreadCall_hpp_
#define slic3r_AgentBridgeUiThreadCall_hpp_

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

#include <boost/asio/io_context.hpp>

#include "AgentBridgeTypes.hpp"

namespace Slic3r { namespace GUI { namespace AgentBridge {

// Queues a function on the UI thread. wxEvtHandler::CallAfter in the app; replaced in tests.
using UiPoster = std::function<void(std::function<void()>)>;

// Runs `work` on the UI thread and delivers its response to `done` on the io thread, which never
// blocks, so a stuck UI thread cannot wedge the server.
//
// If the UI thread has not started `work` within `timeout`, `done` receives studio_busy and `work`
// is skipped when it eventually comes up. Callers can rely on that: a request answered as busy never
// has an effect later.
//
// Once `work` has started it runs to completion. Its response is delivered, unless `on_overrun` is
// given, `timeout` passes first, as when `work` sits inside a dialog waiting for the user, and
// on_overrun (called on the io thread) returns a response: `done` then gets that, and the late
// response is dropped. If it returns nullopt, the call keeps waiting for the real response.
// `done` is called exactly once either way.
void call_on_ui_thread(std::shared_ptr<boost::asio::io_context> io,
                       const UiPoster                          &post,
                       std::chrono::milliseconds                timeout,
                       std::function<BridgeResponse()>          work,
                       std::function<void(BridgeResponse)>      done,
                       std::function<std::optional<BridgeResponse>()> on_overrun = nullptr);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
