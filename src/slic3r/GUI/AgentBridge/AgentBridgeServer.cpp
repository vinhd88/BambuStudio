#include "AgentBridgeServer.hpp"

#include <chrono>
#include <optional>
#include <stdexcept>

#include <boost/asio/post.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#else
#include <process.h>
#endif

#include "AgentBridgeRequestGuard.hpp"

namespace beast = boost::beast;
namespace http  = boost::beast::http;
namespace asio  = boost::asio;
namespace fs    = boost::filesystem;
using tcp       = boost::asio::ip::tcp;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
const std::string TOOL_PATH_PREFIX = "/v1/tools/";
constexpr size_t  MAX_REQUEST_BODY = 64 * 1024;
// Connections are accepted before auth, so a local process could otherwise exhaust Studio's file
// descriptors. The adapter only ever needs a few.
constexpr int     MAX_OPEN_SESSIONS = 32;

std::optional<std::string> header_value(const http::request<http::string_body> &req, http::field field)
{
    auto it = req.find(field);
    if (it == req.end())
        return std::nullopt;
    return std::string(it->value());
}

// One request per connection. Holds the io_context so sockets outlive any owner of the session,
// including a UI-thread closure that runs after the server stopped.
class Session : public std::enable_shared_from_this<Session>
{
public:
    Session(std::shared_ptr<asio::io_context> io, tcp::socket socket, const ToolHandler &handler, std::string token, uint16_t port, std::shared_ptr<std::atomic<int>> open_sessions)
        : m_io(std::move(io)), m_stream(std::move(socket)), m_handler(handler), m_token(std::move(token)), m_port(port), m_open_sessions(std::move(open_sessions))
    {
        m_parser.body_limit(MAX_REQUEST_BODY);
        ++*m_open_sessions;
    }
    ~Session() { --*m_open_sessions; }

    void run()
    {
        // A client that connects and goes quiet must not hold the connection forever.
        m_stream.expires_after(std::chrono::seconds(10));
        http::async_read(m_stream, m_buffer, m_parser, [self = shared_from_this()](beast::error_code ec, size_t) {
            if (ec == http::error::body_limit || ec == http::error::header_limit)
                return self->write(error_response(413, "request_too_large", "Request too large"));
            if (ec)
                return self->close();
            self->handle_request();
        });
    }

private:
    void handle_request()
    {
        const auto &req = m_parser.get();

        RequestHeaders headers;
        headers.host          = header_value(req, http::field::host);
        headers.origin        = header_value(req, http::field::origin);
        headers.authorization = header_value(req, http::field::authorization);
        // Auth comes before routing so an unauthenticated caller learns nothing about the API.
        if (auto rejection = check_request(headers, m_token, m_port))
            return write(*rejection);

        const std::string target(req.target());
        if (target.compare(0, TOOL_PATH_PREFIX.size(), TOOL_PATH_PREFIX) != 0 || target.size() == TOOL_PATH_PREFIX.size())
            return write(error_response(404, "not_found", "Unknown path"));
        if (req.method() != http::verb::post)
            return write(error_response(405, "method_not_allowed", "Use POST"));

        // A UI call can take a while; the read deadline must not fire on the idle socket meanwhile.
        m_stream.expires_never();
        try {
            m_handler(target.substr(TOOL_PATH_PREFIX.size()), req.body(), [self = shared_from_this()](BridgeResponse response) {
                // `done` can arrive from any thread; socket work must stay on the io thread.
                asio::post(*self->m_io, [self, response = std::move(response)] { self->write(response); });
            });
        } catch (const std::exception &e) {
            write(error_response(500, "internal_error", e.what()));
        }
    }

    void write(const BridgeResponse &response)
    {
        auto res = std::make_shared<http::response<http::string_body>>(static_cast<http::status>(response.status), 11);
        res->set(http::field::content_type, "application/json");
        res->keep_alive(false);
        res->body() = response.body;
        res->prepare_payload();

        m_stream.expires_after(std::chrono::seconds(30));
        http::async_write(m_stream, *res, [self = shared_from_this(), res](beast::error_code, size_t) { self->close(); });
    }

    void close()
    {
        beast::error_code ignored;
        m_stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
    }

    std::shared_ptr<asio::io_context>       m_io;
    beast::tcp_stream                       m_stream;
    beast::flat_buffer                      m_buffer;
    http::request_parser<http::string_body> m_parser;
    ToolHandler                             m_handler;
    std::string                             m_token;
    uint16_t                                m_port;
    std::shared_ptr<std::atomic<int>>       m_open_sessions;
};

int current_pid()
{
#ifdef _WIN32
    return _getpid();
#else
    return static_cast<int>(getpid());
#endif
}

// Created with owner-only permissions, never widened after the fact, and renamed into place so a
// reader cannot see a partial file.
void write_owner_only_file(const std::string &path, const std::string &content)
{
    const std::string tmp_path = path + ".tmp";
#ifndef _WIN32
    ::unlink(tmp_path.c_str());
    const int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0)
        throw std::runtime_error("cannot create discovery file");
    const ssize_t written = ::write(fd, content.data(), content.size());
    ::close(fd);
    if (written != static_cast<ssize_t>(content.size())) {
        ::unlink(tmp_path.c_str());
        throw std::runtime_error("cannot write discovery file");
    }
#else
    // Inherits the ACL of the data dir, which already sits in the user's private profile folder.
    boost::nowide::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    out << content;
    out.close();
    if (!out)
        throw std::runtime_error("cannot write discovery file");
#endif
    fs::rename(tmp_path, path);
}
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

    write_discovery_file();

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

    remove_discovery_file();

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
            std::make_shared<Session>(m_io, std::move(socket), m_handler, m_token, m_port, m_open_sessions)->run();
        do_accept();
    });
}

void AgentBridgeServer::write_discovery_file() const
{
    const nlohmann::json discovery = {{"port", m_port}, {"token", m_token}, {"pid", current_pid()}};
    write_owner_only_file(m_discovery_path, discovery.dump());
}

void AgentBridgeServer::remove_discovery_file() const
{
    // A second instance on the same data dir may have replaced the file; leave its copy in place.
    try {
        boost::nowide::ifstream in(m_discovery_path);
        nlohmann::json          discovery;
        in >> discovery;
        if (discovery.value("token", std::string()) != m_token)
            return;
    } catch (const std::exception &) {
        return;
    }
    boost::system::error_code ignored;
    fs::remove(m_discovery_path, ignored);
}

}}} // namespace Slic3r::GUI::AgentBridge
