#ifndef slic3r_AgentBridgeServer_hpp_
#define slic3r_AgentBridgeServer_hpp_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>

#include "AgentBridgeHttpSession.hpp"

namespace Slic3r { namespace GUI { namespace AgentBridge {


// Minimal HTTP server on 127.0.0.1 and a random free port. Modelled on GUI/HttpServer, which cannot
// be extended: it has a fixed port, no auth, and sends unknown routes to the login flow.
class AgentBridgeServer
{
public:
    AgentBridgeServer(std::string discovery_path, ToolHandler handler);
    ~AgentBridgeServer();

    AgentBridgeServer(const AgentBridgeServer &) = delete;
    AgentBridgeServer &operator=(const AgentBridgeServer &) = delete;

    // Binds, publishes the discovery file and starts serving. Throws on failure.
    void start();
    // Stops serving and removes the discovery file. Safe to call repeatedly.
    void stop();

    uint16_t                                 port() const { return m_port; }
    const std::string                       &token() const { return m_token; }
    std::shared_ptr<boost::asio::io_context> io() const { return m_io; }

private:
    void do_accept();

    std::string                              m_discovery_path;
    ToolHandler                              m_handler;
    std::shared_ptr<boost::asio::io_context> m_io;
    boost::asio::ip::tcp::acceptor           m_acceptor;
    boost::asio::steady_timer                m_accept_retry;
    // Shared with sessions, which can outlive the server.
    std::shared_ptr<std::atomic<int>>        m_open_sessions;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> m_work_guard;
    std::thread                              m_thread;
    std::atomic<bool>                        m_running{false};
    std::string                              m_token;
    uint16_t                                 m_port{0};
};

}}} // namespace Slic3r::GUI::AgentBridge

#endif
