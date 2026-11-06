#include "AgentBridgeHttpSession.hpp"

#include <chrono>
#include <optional>

#include <boost/asio/post.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include "AgentBridgeRequestGuard.hpp"

namespace beast = boost::beast;
namespace http  = boost::beast::http;
namespace asio  = boost::asio;
using tcp       = boost::asio::ip::tcp;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
const std::string TOOL_PATH_PREFIX = "/v1/tools/";
constexpr size_t  MAX_REQUEST_BODY = 64 * 1024;

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
} // namespace

void start_http_session(std::shared_ptr<asio::io_context> io, tcp::socket socket, const ToolHandler &handler, const std::string &token, uint16_t port, std::shared_ptr<std::atomic<int>> open_sessions)
{
    std::make_shared<Session>(std::move(io), std::move(socket), handler, token, port, std::move(open_sessions))->run();
}

}}} // namespace Slic3r::GUI::AgentBridge
