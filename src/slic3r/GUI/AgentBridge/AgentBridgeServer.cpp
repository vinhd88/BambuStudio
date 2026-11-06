#include "AgentBridgeServer.hpp"

#include <chrono>

#include <boost/log/trivial.hpp>

#include "AgentBridgeDiscoveryFile.hpp"
#include "AgentBridgeRequestGuard.hpp"

namespace asio = boost::asio;
using tcp      = boost::asio::ip::tcp;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
// Connections are accepted before auth, so a local process could otherwise exhaust Studio's file
// descriptors. The adapter only ever needs a few.
constexpr int MAX_OPEN_SESSIONS = 32;
} // namespace

AgentBridgeServer::AgentBridgeServer(std::string discovery_path, ToolHandler handler)
    : m_discovery_path(std::move(discovery_path))
    , m_handler(std::move(handler))
    , m_io(std::make_shared<asio::io_context>(1))
    , m_acceptor(*m_io)
    , m_accept_retry(*m_io)
    , m_open_sessions(std::make_shared<std::atomic<int>>(0))
{}

AgentBridgeServer::~AgentBridgeServer() { stop(); }

void AgentBridgeServer::start()
{
    if (m_running.load())
        return;

    m_token = generate_token();

    const tcp::endpoint endpoint(asio::ip::address_v4::loopback(), 0);
    m_acceptor.open(endpoint.protocol());
    m_acceptor.bind(endpoint);
    m_acceptor.listen();
    m_port = m_acceptor.local_endpoint().port();

    write_discovery_file(m_discovery_path, m_port, m_token);

    m_running.store(true);
    m_work_guard = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(m_io->get_executor());
    do_accept();
    m_thread = std::thread([io = m_io] {
        // An exception leaving this thread would terminate Studio. Losing the bridge is better.
        try {
            io->run();
        } catch (const std::exception &e) {
            BOOST_LOG_TRIVIAL(error) << "agent bridge: server thread stopped: " << e.what();
        }
    });
}

void AgentBridgeServer::stop()
{
    if (!m_running.exchange(false))
        return;

    remove_discovery_file(m_discovery_path, m_token);

    m_work_guard.reset();
    m_io->stop();
    if (m_thread.joinable())
        m_thread.join();

    // Only safe once the io thread has gone: the acceptor is not thread safe.
    boost::system::error_code ignored;
    m_acceptor.close(ignored);
}

void AgentBridgeServer::do_accept()
{
    m_acceptor.async_accept([this](const boost::system::error_code &ec, tcp::socket socket) {
        if (!m_running.load() || ec == asio::error::operation_aborted)
            return;
        if (ec) {
            // Typically out of file descriptors. Retrying at once would spin this thread and flood the log.
            BOOST_LOG_TRIVIAL(warning) << "agent bridge: accept failed: " << ec.message();
            m_accept_retry.expires_after(std::chrono::milliseconds(100));
            m_accept_retry.async_wait([this](const boost::system::error_code &wait_ec) {
                if (!wait_ec && m_running.load())
                    do_accept();
            });
            return;
        }
        // Over the limit the socket closes as it goes out of scope, and the client sees a dropped connection.
        if (m_open_sessions->load() < MAX_OPEN_SESSIONS)
            start_http_session(m_io, std::move(socket), m_handler, m_token, m_port, m_open_sessions);
        do_accept();
    });
}

}}} // namespace Slic3r::GUI::AgentBridge
