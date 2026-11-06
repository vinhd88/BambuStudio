#include "AgentBridgeUiThreadCall.hpp"

#include <atomic>

#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
struct CallState
{
    // Declared first so it is destroyed last: timer and `done` may own sockets that need the io_context.
    std::shared_ptr<boost::asio::io_context> io;
    boost::asio::steady_timer                timer;
    std::function<BridgeResponse()>          work;
    std::function<void(BridgeResponse)>      done;
    std::function<std::optional<BridgeResponse>()> on_overrun;
    // Whichever of the UI thread and the timer sets this first decides whether `work` runs at all.
    std::atomic<bool>                        claimed{false};
    // Only touched on the io thread.
    bool                                     responded{false};

    explicit CallState(std::shared_ptr<boost::asio::io_context> io_in) : io(std::move(io_in)), timer(*io) {}
};
} // namespace

void call_on_ui_thread(std::shared_ptr<boost::asio::io_context> io,
                       const UiPoster                          &post,
                       std::chrono::milliseconds                timeout,
                       std::function<BridgeResponse()>          work,
                       std::function<void(BridgeResponse)>      done,
                       std::function<std::optional<BridgeResponse>()> on_overrun)
{
    auto state  = std::make_shared<CallState>(std::move(io));
    state->work = std::move(work);
    state->done = std::move(done);
    state->on_overrun = std::move(on_overrun);

    state->timer.expires_after(timeout);
    state->timer.async_wait([state](const boost::system::error_code &ec) {
        if (ec)
            return;
        if (!state->claimed.exchange(true)) {
            state->responded = true;
            BridgeResponse busy = error_response(503, "studio_busy", "Studio did not respond in time. It may be busy or waiting on a dialog.");
            busy.work_ran       = false;
            state->done(std::move(busy));
        } else if (state->on_overrun) {
            if (std::optional<BridgeResponse> early = state->on_overrun()) {
                state->responded = true;
                state->done(std::move(*early));
            }
        }
    });

    post([state] {
        if (state->claimed.exchange(true))
            return;

        BridgeResponse response;
        try {
            response = state->work();
        } catch (const BridgeError &e) {
            response = error_response(e.status, e.code, e.what());
        } catch (const std::exception &e) {
            response = error_response(500, "internal_error", e.what());
        }

        boost::asio::post(*state->io, [state, response = std::move(response)]() mutable {
            // The error overload: a throw here would take down the server thread over a harmless timer.
            boost::system::error_code ignored;
            state->timer.cancel(ignored);
            if (!state->responded)
                state->done(std::move(response));
        });
    });
}

}}} // namespace Slic3r::GUI::AgentBridge
