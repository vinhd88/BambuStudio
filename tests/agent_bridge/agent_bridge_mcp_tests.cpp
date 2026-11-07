// The --mcp-stdio helper: MCP protocol handling, the client that forwards calls to a running Studio,
// and the stdin/stdout loop. The client tests use a real bridge server on a real socket.
#include <catch2/catch.hpp>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include <unistd.h>

#include "slic3r/GUI/AgentBridge/AgentBridgeDiscoveryFile.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeMcpProtocol.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeMcpStdio.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeServer.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeStudioClient.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeToolDefinitions.hpp"

using namespace Slic3r::GUI::AgentBridge;
using nlohmann::json;
namespace fs = boost::filesystem;

namespace {
json rpc(const std::string &line, const ToolCaller &caller = nullptr)
{
    const auto reply = handle_mcp_message(line, caller ? caller : [](const std::string &, const json &) { return ToolCallOutcome{}; });
    REQUIRE(reply.has_value());
    return json::parse(*reply);
}

std::string request(const std::string &method, const json &params = json::object(), int id = 1)
{
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}}.dump();
}

struct TempDir
{
    fs::path dir = fs::temp_directory_path() / fs::unique_path("agent-mcp-%%%%%%%%");
    TempDir() { fs::create_directories(dir); }
    ~TempDir() { fs::remove_all(dir); }
    void write(const std::string &text) const
    {
        boost::nowide::ofstream out((dir / "agent-bridge.json").string());
        out << text;
    }
};
} // namespace

TEST_CASE("tool definitions list the 16 bridge tools with schemas", "[agent_bridge]")
{
    const json &tools = tool_definitions();
    REQUIRE(tools.is_array());
    CHECK(tools.size() == 16);
    for (const json &tool : tools) {
        CHECK(tool["name"].is_string());
        CHECK(tool["description"].is_string());
        CHECK(tool["inputSchema"]["type"] == "object");
    }
    CHECK(tools[0]["name"] == "get_studio_state");

    // Every tool the dispatcher serves, and nothing else.
    std::set<std::string> names;
    for (const json &tool : tools)
        names.insert(tool["name"].get<std::string>());
    CHECK(names == std::set<std::string>{"apply_settings", "describe_settings", "get_filament", "get_job", "get_project", "get_settings",
                                         "get_slice_result", "get_studio_state", "list_filaments", "list_presets", "open_in_official_studio",
                                         "revert_change", "select_filament", "select_process_preset", "set_object_filament", "slice_plate"});
}

TEST_CASE("initialize negotiates the protocol version", "[agent_bridge]")
{
    const json known = rpc(request("initialize", {{"protocolVersion", "2025-03-26"}}));
    CHECK(known["id"] == 1);
    CHECK(known["result"]["protocolVersion"] == "2025-03-26");
    CHECK(known["result"]["capabilities"].contains("tools"));
    CHECK(known["result"]["serverInfo"]["name"] == "bambu-studio");

    const json unknown = rpc(request("initialize", {{"protocolVersion", "1999-01-01"}}));
    CHECK(unknown["result"]["protocolVersion"] == MCP_PROTOCOL_VERSIONS[0]);
}

TEST_CASE("notifications and client responses get no reply", "[agent_bridge]")
{
    const ToolCaller none = [](const std::string &, const json &) { return ToolCallOutcome{}; };
    CHECK_FALSE(handle_mcp_message(R"({"jsonrpc":"2.0","method":"notifications/initialized"})", none));
    CHECK_FALSE(handle_mcp_message(R"({"jsonrpc":"2.0","method":"no/such/notification"})", none));
    CHECK_FALSE(handle_mcp_message(R"({"jsonrpc":"2.0","id":7,"result":{}})", none));
}

TEST_CASE("ping, tools/list and unknown methods", "[agent_bridge]")
{
    CHECK(rpc(request("ping"))["result"] == json::object());
    CHECK(rpc(request("tools/list"))["result"]["tools"] == tool_definitions());
    CHECK(rpc(request("resources/list"))["error"]["code"] == -32601);
}

TEST_CASE("malformed messages get JSON-RPC errors", "[agent_bridge]")
{
    CHECK(rpc("{not json")["error"]["code"] == -32700);
    CHECK(rpc(R"([{"jsonrpc":"2.0","id":1,"method":"ping"}])")["error"]["code"] == -32600);
    CHECK(rpc(R"({"id":1,"method":"ping"})")["error"]["code"] == -32600);
    CHECK(rpc(R"("text")")["error"]["code"] == -32600);
    CHECK(rpc(request("tools/call", {{"arguments", json::object()}}))["error"]["code"] == -32602);
    CHECK(rpc(request("tools/call", {{"name", "no_such_tool"}}))["error"]["code"] == -32602);
    CHECK(rpc(request("tools/call", {{"name", "get_project"}, {"arguments", json::array()}}))["error"]["code"] == -32602);
}

TEST_CASE("fields of the wrong type are answered, never crash the helper", "[agent_bridge]")
{
    CHECK(rpc(R"({"jsonrpc":2,"id":1,"method":"ping"})")["error"]["code"] == -32600);
    CHECK(rpc(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":1}})")["result"]["protocolVersion"] == MCP_PROTOCOL_VERSIONS[0]);
    CHECK(rpc(R"({"jsonrpc":"2.0","id":1,"method":7})")["error"]["code"] == -32600);
    CHECK(rpc(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":"x"})")["error"]["code"] == -32602);
    const ToolCaller throws = [](const std::string &, const json &) -> ToolCallOutcome { throw std::runtime_error("boom"); };
    const json internal = rpc(request("tools/call", {{"name", "get_project"}}), throws);
    CHECK(internal["id"] == 1);
    CHECK(internal["error"]["code"] == -32603);
}

TEST_CASE("ids must be strings or integers", "[agent_bridge]")
{
    CHECK(rpc(R"({"jsonrpc":"2.0","id":"abc","method":"ping"})")["id"] == "abc");
    const json object_id = rpc(R"({"jsonrpc":"2.0","id":{"a":1},"method":"ping"})");
    CHECK(object_id["error"]["code"] == -32600);
    CHECK(object_id["id"].is_null());
    CHECK(rpc(R"({"jsonrpc":"2.0","id":null,"method":"ping"})")["error"]["code"] == -32600);
}

TEST_CASE("tools/call passes results and errors through as the Node adapter did", "[agent_bridge]")
{
    std::string seen_tool;
    json        seen_args;
    const ToolCaller ok = [&](const std::string &tool, const json &args) {
        seen_tool = tool;
        seen_args = args;
        return ToolCallOutcome{true, {{"object_count", 2}}, "", ""};
    };
    const json result = rpc(request("tools/call", {{"name", "get_studio_state"}}), ok)["result"];
    CHECK(seen_tool == "get_studio_state");
    CHECK(seen_args == json::object());
    CHECK_FALSE(result.contains("isError"));
    CHECK(result["content"][0]["type"] == "text");
    CHECK(result["content"][0]["text"] == R"({"object_count":2})");

    const ToolCaller fails = [](const std::string &, const json &) { return ToolCallOutcome{false, nullptr, "studio_busy", "A dialog is open."}; };
    const json error = rpc(request("tools/call", {{"name", "apply_settings"}, {"arguments", {{"changes", {{"a", 1}}}}}}), fails)["result"];
    CHECK(error["isError"] == true);
    CHECK(error["content"][0]["text"] == "studio_busy: A dialog is open.");
}

TEST_CASE("the Studio client reports why it cannot reach Studio", "[agent_bridge]")
{
    TempDir temp;
    const auto code = [&](const std::string &data_dir) { return call_running_studio(data_dir, "get_studio_state", json::object()).code; };

    CHECK(code("") == "not_configured");
    CHECK(code(temp.dir.string()) == "studio_not_running");
    temp.write("{oops");
    CHECK(code(temp.dir.string()) == "discovery_unreadable");
    temp.write(R"({"port":"x","token":"t","pid":1})");
    CHECK(code(temp.dir.string()) == "discovery_unreadable");
    // A pid no process has: the file is left over from a crash.
    temp.write(R"({"port":1,"token":"t","pid":999999})");
    CHECK(code(temp.dir.string()) == "studio_not_running");
}

TEST_CASE("the Studio client calls a running bridge with its token", "[agent_bridge]")
{
    TempDir temp;
    // Holding the reply keeps the connection open, as a call still waiting on Studio's UI thread does.
    std::vector<std::function<void(BridgeResponse)>> pending;
    AgentBridgeServer server((temp.dir / "agent-bridge.json").string(), [&pending](const std::string &tool, const std::string &body, std::function<void(BridgeResponse)> done) {
        if (tool == "slow")
            return pending.push_back(std::move(done)); // Not answered in time; the client's deadline must fire.
        if (tool == "broken")
            return done({200, "not json"});
        if (tool == "refuses")
            return done(error_response(409, "waiting_for_user", "A dialog is open."));
        done(ok_response({{"tool", tool}, {"args", json::parse(body)}}));
    });
    server.start();

    const ToolCallOutcome ok = call_running_studio(temp.dir.string(), "get_settings", {{"scope", "object"}});
    REQUIRE(ok.ok);
    CHECK(ok.result["tool"] == "get_settings");
    CHECK(ok.result["args"]["scope"] == "object");

    const ToolCallOutcome refused = call_running_studio(temp.dir.string(), "refuses", json::object());
    CHECK_FALSE(refused.ok);
    CHECK(refused.code == "waiting_for_user");
    CHECK(refused.message == "A dialog is open.");

    CHECK(call_running_studio(temp.dir.string(), "broken", json::object()).code == "bad_response");
    CHECK(call_running_studio(temp.dir.string(), "slow", json::object(), std::chrono::milliseconds(300)).code == "studio_busy");

    // Stopped: the discovery file is gone.
    server.stop();
    CHECK(call_running_studio(temp.dir.string(), "get_settings", json::object()).code == "studio_not_running");
}

TEST_CASE("the stdio loop answers each request on its own line and ignores blank lines", "[agent_bridge]")
{
    TempDir           temp;
    std::stringstream in, out;
    in << request("initialize", {{"protocolVersion", "2025-06-18"}}, 1) << "\n"
       << R"({"jsonrpc":"2.0","method":"notifications/initialized"})" << "\n\n"
       << request("tools/call", {{"name", "get_studio_state"}}, 2) << "\r\n";
    CHECK(run_mcp_stdio(temp.dir.string(), in, out) == 0);

    std::string first, second, extra;
    REQUIRE(std::getline(out, first));
    REQUIRE(std::getline(out, second));
    CHECK_FALSE(std::getline(out, extra));
    CHECK(json::parse(first)["id"] == 1);
    const json call = json::parse(second);
    CHECK(call["id"] == 2);
    CHECK(call["result"]["content"][0]["text"].get<std::string>().rfind("studio_not_running: ", 0) == 0);
}

TEST_CASE("a discovery file is removed only when its process is gone", "[agent_bridge]")
{
    TempDir     temp;
    const auto  path = (temp.dir / "agent-bridge.json").string();
    temp.write(R"({"port":1,"token":"t","pid":999999})");
    remove_stale_discovery_file(path);
    CHECK_FALSE(fs::exists(path));

    temp.write(R"({"port":1,"token":"t","pid":)" + std::to_string(::getpid()) + "}");
    remove_stale_discovery_file(path);
    CHECK(fs::exists(path));

    temp.write("{broken");
    remove_stale_discovery_file(path);
    CHECK_FALSE(fs::exists(path));
}

TEST_CASE("--mcp-stdio arguments", "[agent_bridge]")
{
    const char *with_dir[] = {"BambuStudio", "--mcp-stdio", "--datadir", "/tmp/agent-data"};
    CHECK(is_mcp_stdio_run(4, const_cast<char **>(with_dir)));
    CHECK(mcp_stdio_data_dir(4, const_cast<char **>(with_dir)) == "/tmp/agent-data");

    const char *gui[] = {"BambuStudio", "project.3mf"};
    CHECK_FALSE(is_mcp_stdio_run(2, const_cast<char **>(gui)));

    // No flag and no env: the folder Studio itself uses without --datadir.
    const char *saved = std::getenv("BAMBU_AGENT_DATA_DIR");
    const std::string saved_value = saved ? saved : "";
    ::unsetenv("BAMBU_AGENT_DATA_DIR");
    const char *bare[] = {"BambuStudio", "--mcp-stdio"};
    const std::string fallback = mcp_stdio_data_dir(2, const_cast<char **>(bare));
    CHECK(fallback == std::string(std::getenv("HOME")) + "/Library/Application Support/BambuStudioAgent");
    ::setenv("BAMBU_AGENT_DATA_DIR", "/tmp/from-env", 1);
    CHECK(mcp_stdio_data_dir(2, const_cast<char **>(bare)) == "/tmp/from-env");
    if (saved)
        ::setenv("BAMBU_AGENT_DATA_DIR", saved_value.c_str(), 1);
    else
        ::unsetenv("BAMBU_AGENT_DATA_DIR");
}
