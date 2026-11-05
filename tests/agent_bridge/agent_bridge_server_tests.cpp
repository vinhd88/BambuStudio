#include <catch2/catch.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/connect.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include "slic3r/GUI/AgentBridge/AgentBridgeServer.hpp"

using namespace Slic3r::GUI::AgentBridge;
using nlohmann::json;
namespace beast = boost::beast;
namespace http  = boost::beast::http;
namespace asio  = boost::asio;
namespace fs    = boost::filesystem;

namespace {
struct Reply
{
    int  status;
    json body;
};

struct RequestOptions
{
    std::string target = "/v1/tools/echo";
    http::verb  method = http::verb::post;
    std::string token;
    std::string host;
    std::string origin;
    std::string body = "{}";
};

// Real HTTP over a real socket, so these tests exercise the server's parsing as well as its rules.
Reply send_request(uint16_t port, const RequestOptions &options)
{
    asio::io_context  io;
    beast::tcp_stream stream(io);
    stream.connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), port));

    http::request<http::string_body> req(options.method, options.target, 11);
    req.set(http::field::host, options.host.empty() ? "127.0.0.1:" + std::to_string(port) : options.host);
    if (!options.token.empty())
        req.set(http::field::authorization, "Bearer " + options.token);
    if (!options.origin.empty())
        req.set(http::field::origin, options.origin);
    req.body() = options.body;
    req.prepare_payload();
    http::write(stream, req);

    beast::flat_buffer                buffer;
    http::response<http::string_body> res;
    http::read(stream, buffer, res);
    return {static_cast<int>(res.result_int()), res.body().empty() ? json() : json::parse(res.body())};
}

std::string read_file(const fs::path &path)
{
    boost::nowide::ifstream in(path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct ServerFixture
{
    fs::path          dir  = fs::temp_directory_path() / fs::unique_path("agent-bridge-%%%%%%%%");
    fs::path          file = dir / "agent-bridge.json";
    AgentBridgeServer server;

    ServerFixture()
        : server((fs::create_directories(dir), file.string()), [](const std::string &tool, const std::string &body, std::function<void(BridgeResponse)> done) {
            if (tool == "throws")
                throw std::runtime_error("handler failed");
            done(ok_response({{"tool", tool}, {"body", body}}));
        })
    {
        server.start();
    }
    ~ServerFixture()
    {
        server.stop();
        fs::remove_all(dir);
    }
    RequestOptions authed() const
    {
        RequestOptions options;
        options.token = server.token();
        return options;
    }
};
} // namespace

TEST_CASE_METHOD(ServerFixture, "server routes authenticated calls to the handler", "[agent_bridge]")
{
    RequestOptions options = authed();
    options.body           = R"({"a":1})";
    const Reply reply      = send_request(server.port(), options);
    CHECK(reply.status == 200);
    CHECK(reply.body["ok"] == true);
    CHECK(reply.body["result"]["tool"] == "echo");
    CHECK(reply.body["result"]["body"] == R"({"a":1})");
}

TEST_CASE_METHOD(ServerFixture, "server rejects requests that fail the checks, without reaching the handler", "[agent_bridge]")
{
    RequestOptions options = authed();
    auto expect_rejected   = [&](int status, const std::string &code) {
        const Reply reply = send_request(server.port(), options);
        CHECK(reply.status == status);
        CHECK(reply.body["error"]["code"] == code);
    };

    SECTION("no token")
    {
        options.token.clear();
        expect_rejected(401, "bad_token");
    }
    SECTION("wrong token")
    {
        options.token = "wrong";
        expect_rejected(401, "bad_token");
    }
    SECTION("browser origin with a valid token")
    {
        options.origin = "https://evil.example";
        expect_rejected(403, "origin_rejected");
    }
    SECTION("non-loopback host with a valid token")
    {
        options.host = "evil.example";
        expect_rejected(403, "host_rejected");
    }
    SECTION("auth is checked before routing")
    {
        options.token.clear();
        options.target = "/anything";
        expect_rejected(401, "bad_token");
    }
    SECTION("unknown path")
    {
        options.target = "/v2/other";
        expect_rejected(404, "not_found");
    }
    SECTION("tool path with no tool")
    {
        options.target = "/v1/tools/";
        expect_rejected(404, "not_found");
    }
    SECTION("GET")
    {
        options.method = http::verb::get;
        options.body.clear();
        expect_rejected(405, "method_not_allowed");
    }
    SECTION("oversized body")
    {
        options.body = std::string(70 * 1024, 'x');
        expect_rejected(413, "request_too_large");
    }
    SECTION("handler exception")
    {
        options.target = "/v1/tools/throws";
        expect_rejected(500, "internal_error");
    }
}

TEST_CASE_METHOD(ServerFixture, "discovery file publishes the port and token privately", "[agent_bridge]")
{
    const json discovery = json::parse(read_file(file));
    CHECK(discovery["port"] == server.port());
    CHECK(discovery["token"] == server.token());
    CHECK(discovery["pid"].get<int>() > 0);
    CHECK(server.port() != 0);
#ifndef _WIN32
    CHECK(fs::status(file).permissions() == fs::owner_read + fs::owner_write);
#endif
    CHECK_FALSE(fs::exists(file.string() + ".tmp"));
}

TEST_CASE_METHOD(ServerFixture, "stop removes the discovery file and closes the port", "[agent_bridge]")
{
    const uint16_t port = server.port();
    server.stop();
    CHECK_FALSE(fs::exists(file));
    CHECK_THROWS(send_request(port, authed()));
    server.stop(); // repeat is harmless
}

TEST_CASE_METHOD(ServerFixture, "stop leaves a discovery file written by another instance", "[agent_bridge]")
{
    {
        boost::nowide::ofstream out(file.string(), std::ios::trunc);
        out << R"({"port":1,"token":"other-instance","pid":1})";
    }
    server.stop();
    CHECK(fs::exists(file));
}

TEST_CASE_METHOD(ServerFixture, "idle connections beyond the limit are dropped, and capacity returns", "[agent_bridge]")
{
    asio::io_context                                     io;
    const asio::ip::tcp::endpoint                        endpoint(asio::ip::address_v4::loopback(), server.port());
    std::vector<std::unique_ptr<asio::ip::tcp::socket>>  held;
    for (int i = 0; i < 32; ++i) {
        held.push_back(std::make_unique<asio::ip::tcp::socket>(io));
        held.back()->connect(endpoint);
    }
    // Requests are handled in order, so a round trip means every connection above has been accepted.
    const auto rejected_now = [&] {
        try {
            send_request(server.port(), authed());
            return false;
        } catch (const std::exception &) {
            return true;
        }
    };
    CHECK(rejected_now());

    held.clear();
    bool recovered = false;
    for (int attempt = 0; attempt < 200 && !recovered; ++attempt) {
        recovered = !rejected_now();
        if (!recovered)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(recovered);
}

TEST_CASE_METHOD(ServerFixture, "each start uses a fresh token", "[agent_bridge]")
{
    AgentBridgeServer other((dir / "other.json").string(), [](const std::string &, const std::string &, std::function<void(BridgeResponse)> done) { done(ok_response({})); });
    other.start();
    CHECK(other.token() != server.token());
    CHECK(other.port() != server.port());
    other.stop();
}
