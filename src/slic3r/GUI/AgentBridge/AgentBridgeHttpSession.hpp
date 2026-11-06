#ifndef slic3r_AgentBridgeHttpSession_hpp_
#define slic3r_AgentBridgeHttpSession_hpp_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "AgentBridgeTypes.hpp"

namespace Slic3r { namespace GUI { namespace AgentBridge {

// Receives an authenticated POST /v1/tools/<tool>. Called on the io thread, so it must not block;
// `done` may be called from any thread.
using ToolHandler = std::function<void(const std::string &tool, const std::string &body, std::function<void(BridgeResponse)> done)>;

// Serves one request on `socket`, then closes it. The session keeps itself and the io_context alive
// until then. `open_sessions` counts live sessions, so the server can cap them.
void start_http_session(std::shared_ptr<boost::asio::io_context> io, boost::asio::ip::tcp::socket socket, const ToolHandler &handler,
                        const std::string &token, uint16_t port, std::shared_ptr<std::atomic<int>> open_sessions);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
