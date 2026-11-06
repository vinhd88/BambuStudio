#include "AgentBridgeStudioClient.hpp"

#include <cerrno>
#include <csignal>
#include <iterator>
#include <optional>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include "AgentBridgeTypes.hpp"

namespace beast = boost::beast;
namespace http  = boost::beast::http;
namespace asio  = boost::asio;
namespace fs    = boost::filesystem;
using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
const char *const DISCOVERY_FILE = "agent-bridge.json";

ToolCallOutcome failure(std::string code, std::string message) { return {false, nullptr, std::move(code), std::move(message)}; }

ToolCallOutcome not_running()
{
    return failure("studio_not_running", "Bambu Studio (custom agent build) is not running. Ask the user to start it, then try again.");
}

bool process_alive(int pid)
{
    // EPERM means it exists but belongs to someone else.
    return pid > 0 && (::kill(pid, 0) == 0 || errno == EPERM);
}

struct Discovery
{
    uint16_t    port = 0;
    std::string token;
};

// The discovery file's port and token, or the failure to report instead.
std::optional<ToolCallOutcome> read_discovery(const std::string &data_dir, Discovery &out)
{
    const fs::path path = fs::path(data_dir) / DISCOVERY_FILE;
    boost::system::error_code ec;
    if (!fs::exists(path, ec))
        return not_running();
    boost::nowide::ifstream in(path.string());
    if (!in)
        return failure("discovery_unreadable", "Cannot read the bridge discovery file.");
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    const json discovery = json::parse(text, nullptr, false);
    if (discovery.is_discarded())
        return failure("discovery_unreadable", "The bridge discovery file is not valid JSON.");
    const auto port = discovery.find("port"), token = discovery.find("token"), pid = discovery.find("pid");
    if (port == discovery.end() || !port->is_number_integer() || token == discovery.end() || !token->is_string() || pid == discovery.end() ||
        !pid->is_number_integer() || port->get<int>() <= 0 || port->get<int>() > 65535)
        return failure("discovery_unreadable", "The bridge discovery file has an unexpected format.");
    // A crash leaves the file behind. Checking first also avoids sending the token to whatever now owns that port.
    if (!process_alive(pid->get<int>()))
        return not_running();
    out.port  = static_cast<uint16_t>(port->get<int>());
    out.token = token->get<std::string>();
    return std::nullopt;
}

// One POST to the bridge on 127.0.0.1. Fills `status` and `body`, or returns the failure to report.
std::optional<ToolCallOutcome> post(const Discovery &discovery, const std::string &tool, const json &args, std::chrono::milliseconds timeout, int &status,
                                    std::string &body)
{
    asio::io_context  io;
    beast::tcp_stream stream(io);
    http::request<http::string_body> req(http::verb::post, "/v1/tools/" + tool, 11);
    req.set(http::field::host, "127.0.0.1:" + std::to_string(discovery.port));
    req.set(http::field::authorization, "Bearer " + discovery.token);
    req.set(http::field::content_type, "application/json");
    req.body() = dump_json(args);
    req.prepare_payload();

    beast::flat_buffer                buffer;
    http::response<http::string_body> res;
    beast::error_code                 result;
    // Async with one deadline for the whole exchange: beast's sync calls have no timeout.
    stream.expires_after(timeout);
    stream.async_connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), discovery.port), [&](beast::error_code ec) {
        if (ec)
            return void(result = ec);
        http::async_write(stream, req, [&](beast::error_code ec, size_t) {
            if (ec)
                return void(result = ec);
            http::async_read(stream, buffer, res, [&](beast::error_code ec, size_t) { result = ec; });
        });
    });
    io.run();

    if (result == beast::error::timeout)
        return failure("studio_busy", "Studio did not respond in time. It may be busy or waiting on a dialog.");
    if (result)
        return not_running();
    status = static_cast<int>(res.result_int());
    body   = res.body();
    return std::nullopt;
}
} // namespace

ToolCallOutcome call_running_studio(const std::string &data_dir, const std::string &tool, const json &args, std::chrono::milliseconds timeout)
{
    if (data_dir.empty())
        return failure("not_configured", "The MCP server needs --datadir (or BAMBU_AGENT_DATA_DIR) set to the data folder Studio runs with.");
    try {
        int         status = 0;
        std::string body;
        // Studio may reset its token, which restarts the bridge on a new port, between reading the
        // discovery file and the call. One fresh read covers that; a timeout is not retried.
        for (int attempt = 0;; ++attempt) {
            Discovery discovery;
            if (auto failed = read_discovery(data_dir, discovery))
                return *failed;
            auto failed = post(discovery, tool, args, timeout, status, body);
            const bool stale = (failed && failed->code == "studio_not_running") || (!failed && status == 401);
            if (stale && attempt == 0)
                continue;
            if (failed)
                return *failed;
            break;
        }

        const json payload = json::parse(body, nullptr, false);
        if (payload.is_discarded() || !payload.is_object())
            return failure("bad_response", "The bridge sent an unreadable response (HTTP " + std::to_string(status) + ").");
        if (payload.value("ok", false))
            return {true, payload.value("result", json(nullptr)), "", ""};
        const json error = payload.value("error", json::object());
        return failure(error.is_object() ? error.value("code", "unknown_error") : "unknown_error",
                       error.is_object() ? error.value("message", "HTTP " + std::to_string(status)) : "HTTP " + std::to_string(status));
    } catch (const std::exception &e) {
        return failure("adapter_error", e.what());
    }
}

}}} // namespace Slic3r::GUI::AgentBridge
