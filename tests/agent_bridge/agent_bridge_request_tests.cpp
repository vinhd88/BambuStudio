#include <catch2/catch.hpp>

#include <optional>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <thread>
#include <vector>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>

#include "slic3r/GUI/AgentBridge/AgentBridgeRequestGuard.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeUiThreadCall.hpp"

using namespace Slic3r::GUI::AgentBridge;
using nlohmann::json;

namespace {
const std::string TOKEN = "secret-token";
const uint16_t    PORT  = 4321;

RequestHeaders valid_headers() { return {std::string("127.0.0.1:4321"), std::nullopt, "Bearer " + TOKEN}; }

std::string rejection_code(const RequestHeaders &headers)
{
    const auto rejection = check_request(headers, TOKEN, PORT);
    return rejection ? json::parse(rejection->body)["error"]["code"].get<std::string>() : std::string();
}

// io_context running on its own thread, standing in for the server's io thread.
struct IoThread
{
    std::shared_ptr<boost::asio::io_context> io = std::make_shared<boost::asio::io_context>(1);
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard{io->get_executor()};
    std::thread thread{[this] { io->run(); }};
    ~IoThread()
    {
        guard.reset();
        io->stop();
        thread.join();
    }
};

const auto SHORT_TIMEOUT = std::chrono::milliseconds(50);
const auto WAIT_LIMIT    = std::chrono::seconds(5);
} // namespace

TEST_CASE("generate_token gives distinct 256-bit hex tokens", "[agent_bridge]")
{
    const std::string a = generate_token();
    CHECK(a.size() == 64);
    CHECK(a.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(a != generate_token());
}

TEST_CASE("tokens_equal", "[agent_bridge]")
{
    CHECK(tokens_equal("abc", "abc"));
    CHECK_FALSE(tokens_equal("abc", "abd"));
    CHECK_FALSE(tokens_equal("abc", "abcd"));
    CHECK_FALSE(tokens_equal("", "abc"));
}

TEST_CASE("check_request accepts only loopback hosts with the right token", "[agent_bridge]")
{
    RequestHeaders headers = valid_headers();

    SECTION("valid 127.0.0.1") { CHECK(rejection_code(headers).empty()); }
    SECTION("valid localhost")
    {
        headers.host = "localhost:4321";
        CHECK(rejection_code(headers).empty());
    }
    SECTION("missing host")
    {
        headers.host.reset();
        CHECK(rejection_code(headers) == "host_rejected");
    }
    SECTION("DNS rebinding host")
    {
        headers.host = "evil.example:4321";
        CHECK(rejection_code(headers) == "host_rejected");
    }
    SECTION("look-alike host")
    {
        headers.host = "127.0.0.1.evil.example:4321";
        CHECK(rejection_code(headers) == "host_rejected");
    }
    SECTION("wrong port")
    {
        headers.host = "127.0.0.1:9999";
        CHECK(rejection_code(headers) == "host_rejected");
    }
    SECTION("any origin, even with a valid token")
    {
        headers.origin = "https://evil.example";
        CHECK(rejection_code(headers) == "origin_rejected");
    }
    SECTION("literal null origin")
    {
        headers.origin = "null";
        CHECK(rejection_code(headers) == "origin_rejected");
    }
    SECTION("missing token")
    {
        headers.authorization.reset();
        CHECK(rejection_code(headers) == "bad_token");
    }
    SECTION("wrong token")
    {
        headers.authorization = "Bearer nope";
        CHECK(rejection_code(headers) == "bad_token");
    }
    SECTION("token without the Bearer scheme")
    {
        headers.authorization = TOKEN;
        CHECK(rejection_code(headers) == "bad_token");
    }
    SECTION("empty authorization")
    {
        headers.authorization = "";
        CHECK(rejection_code(headers) == "bad_token");
    }
}

TEST_CASE("call_on_ui_thread", "[agent_bridge]")
{
    IoThread                    io_thread;
    std::promise<BridgeResponse> delivered;
    auto                        done = [&delivered](BridgeResponse response) { delivered.set_value(std::move(response)); };
    auto                        result = delivered.get_future();

    SECTION("delivers the work's response")
    {
        const UiPoster immediately = [](std::function<void()> fn) { std::thread(std::move(fn)).detach(); };
        call_on_ui_thread(io_thread.io, immediately, WAIT_LIMIT, [] { return ok_response({{"x", 1}}); }, done);
        REQUIRE(result.wait_for(WAIT_LIMIT) == std::future_status::ready);
        CHECK(json::parse(result.get().body)["result"]["x"] == 1);
    }

    SECTION("a stuck UI thread gets studio_busy, and the work is skipped if it arrives later")
    {
        std::function<void()> queued;
        const UiPoster        never_runs = [&queued](std::function<void()> fn) { queued = std::move(fn); };
        std::atomic<bool>     work_ran{false};

        call_on_ui_thread(io_thread.io, never_runs, SHORT_TIMEOUT, [&work_ran] { work_ran = true; return ok_response({}); }, done);
        REQUIRE(result.wait_for(WAIT_LIMIT) == std::future_status::ready);
        const BridgeResponse response = result.get();
        CHECK(response.status == 503);
        CHECK(json::parse(response.body)["error"]["code"] == "studio_busy");

        // This is the guarantee phase 3 relies on: a change reported as busy must never apply later.
        queued();
        CHECK_FALSE(work_ran.load());
    }

    SECTION("work that started before the timeout finishes and is delivered")
    {
        const UiPoster immediately = [](std::function<void()> fn) { std::thread(std::move(fn)).detach(); };
        call_on_ui_thread(io_thread.io, immediately, SHORT_TIMEOUT, [] {
            std::this_thread::sleep_for(SHORT_TIMEOUT * 3);
            return ok_response({{"late", true}});
        }, done);
        REQUIRE(result.wait_for(WAIT_LIMIT) == std::future_status::ready);
        CHECK(result.get().status == 200);
    }

    SECTION("work inside a dialog: the client hears on_overrun, once, and the late response is dropped")
    {
        const UiPoster    immediately = [](std::function<void()> fn) { std::thread(std::move(fn)).detach(); };
        std::atomic<int>  deliveries{0};
        std::atomic<bool> finished{false};
        std::promise<void> released;
        auto               release = released.get_future().share();
        call_on_ui_thread(io_thread.io, immediately, SHORT_TIMEOUT, [release, &finished] {
            release.wait();
            finished = true;
            return ok_response({{"late", true}});
        }, [&](BridgeResponse response) { ++deliveries; if (deliveries == 1) delivered.set_value(std::move(response)); },
        [] { return ok_response({{"status", "waiting_for_user"}}); });

        REQUIRE(result.wait_for(WAIT_LIMIT) == std::future_status::ready);
        CHECK(json::parse(result.get().body)["result"]["status"] == "waiting_for_user");

        released.set_value();
        for (int i = 0; i < 100 && !finished.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::this_thread::sleep_for(SHORT_TIMEOUT);
        CHECK(deliveries.load() == 1);
    }

    SECTION("on_overrun returning nothing keeps the call waiting for the real response")
    {
        const UiPoster immediately = [](std::function<void()> fn) { std::thread(std::move(fn)).detach(); };
        call_on_ui_thread(io_thread.io, immediately, SHORT_TIMEOUT, [] {
            std::this_thread::sleep_for(SHORT_TIMEOUT * 3);
            return ok_response({{"late", true}});
        }, done, []() -> std::optional<BridgeResponse> { return std::nullopt; });
        REQUIRE(result.wait_for(WAIT_LIMIT) == std::future_status::ready);
        CHECK(json::parse(result.get().body)["result"]["late"] == true);
    }

    SECTION("a timeout before the work started is marked as having had no effect")
    {
        std::function<void()> queued;
        const UiPoster        never_runs = [&queued](std::function<void()> fn) { queued = std::move(fn); };
        call_on_ui_thread(io_thread.io, never_runs, SHORT_TIMEOUT, [] { return ok_response({}); }, done);
        REQUIRE(result.wait_for(WAIT_LIMIT) == std::future_status::ready);
        CHECK_FALSE(result.get().work_ran);
        queued();
    }

    SECTION("on_overrun is not used when the work finishes in time")
    {
        const UiPoster immediately = [](std::function<void()> fn) { std::thread(std::move(fn)).detach(); };
        call_on_ui_thread(io_thread.io, immediately, WAIT_LIMIT, [] { return ok_response({{"x", 1}}); }, done,
                          [] { return ok_response({{"status", "waiting_for_user"}}); });
        REQUIRE(result.wait_for(WAIT_LIMIT) == std::future_status::ready);
        CHECK(json::parse(result.get().body)["result"]["x"] == 1);
    }

    SECTION("a call the UI thread never started gets studio_busy, not on_overrun")
    {
        std::function<void()> queued;
        const UiPoster        never_runs = [&queued](std::function<void()> fn) { queued = std::move(fn); };
        call_on_ui_thread(io_thread.io, never_runs, SHORT_TIMEOUT, [] { return ok_response({}); }, done,
                          [] { return ok_response({{"status", "waiting_for_user"}}); });
        REQUIRE(result.wait_for(WAIT_LIMIT) == std::future_status::ready);
        CHECK(json::parse(result.get().body)["error"]["code"] == "studio_busy");
        queued();
    }

    SECTION("exceptions become error responses")
    {
        const UiPoster immediately = [](std::function<void()> fn) { std::thread(std::move(fn)).detach(); };

        SECTION("BridgeError keeps its status and code")
        {
            call_on_ui_thread(io_thread.io, immediately, WAIT_LIMIT, []() -> BridgeResponse { throw BridgeError(404, "not_found", "nope"); }, done);
            REQUIRE(result.wait_for(WAIT_LIMIT) == std::future_status::ready);
            const BridgeResponse response = result.get();
            CHECK(response.status == 404);
            CHECK(json::parse(response.body)["error"]["code"] == "not_found");
        }
        SECTION("other exceptions are internal errors")
        {
            call_on_ui_thread(io_thread.io, immediately, WAIT_LIMIT, []() -> BridgeResponse { throw std::runtime_error("boom"); }, done);
            REQUIRE(result.wait_for(WAIT_LIMIT) == std::future_status::ready);
            CHECK(result.get().status == 500);
        }
    }
}
