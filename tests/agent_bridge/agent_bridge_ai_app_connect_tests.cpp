// Registering this Studio in AI app configs: entry text in JSON and TOML, status, and connecting
// through an app's CLI (a fake one in a fake home folder).
#include <catch2/catch.hpp>

#include <cstdlib>
#include <iterator>

#include <signal.h>
#include <string>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include "slic3r/GUI/AgentBridge/AgentBridgeAiAppConfigs.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeAiAppConnect.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeProcessRunner.hpp"

using namespace Slic3r::GUI::AgentBridge;
using nlohmann::json;
namespace fs = boost::filesystem;

namespace {
struct TempHome
{
    fs::path path = fs::temp_directory_path() / fs::unique_path("agent-connect-%%%%-%%%%");
    TempHome() { fs::create_directories(path); }
    ~TempHome()
    {
        boost::system::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string str() const { return path.string(); }
};

void write(const fs::path &path, const std::string &text)
{
    fs::create_directories(path.parent_path());
    boost::nowide::ofstream(path.string()) << text;
}

std::string read(const fs::path &path)
{
    boost::nowide::ifstream in(path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

const McpEntry DEV{"/Apps/Studio Agent.app/Contents/MacOS/BambuStudio", {"--mcp-stdio", "--datadir", "/data/dev"}};

AiApp app_named(const std::string &id, const std::string &home)
{
    for (const AiApp &app : ai_apps(home))
        if (app.id == id)
            return app;
    FAIL("no app " << id);
    return {};
}
} // namespace

TEST_CASE("The entry adds --datadir only away from the default folder", "[agent_bridge][connect]")
{
    CHECK(studio_entry("/A/BambuStudio", "/S/BambuStudioAgent", "/S/BambuStudioAgent").args == std::vector<std::string>{"--mcp-stdio"});
    CHECK(studio_entry("/A/BambuStudio", "/S/BambuStudioAgent/", "/S/BambuStudioAgent").args == std::vector<std::string>{"--mcp-stdio"});
    CHECK(studio_entry("/A/BambuStudio", "/dev", "/S/BambuStudioAgent").args == std::vector<std::string>{"--mcp-stdio", "--datadir", "/dev"});
}

TEST_CASE("JSON configs: read, compare, and write only our entry", "[agent_bridge][connect]")
{
    const std::string config = R"({"preferences":{"z":1,"a":2},"mcpServers":{"other":{"command":"npx","args":["x"]}}})";
    CHECK_FALSE(read_json_entry(config));
    CHECK(compare_entry(read_json_entry(config), DEV) == ConnectionState::NotConnected);

    const std::string written = with_json_entry(config, DEV);
    const json        result  = json::parse(written);
    CHECK(result["mcpServers"]["other"]["command"] == "npx");
    CHECK(result["preferences"]["z"] == 1);
    // Keys stay in their order: "z" before "a", preferences before mcpServers.
    CHECK(written.find("\"z\"") < written.find("\"a\""));
    CHECK(written.find("preferences") < written.find("mcpServers"));
    CHECK(compare_entry(read_json_entry(written), DEV) == ConnectionState::Connected);

    McpEntry other_build = DEV;
    other_build.command  = "/elsewhere/BambuStudio";
    CHECK(compare_entry(read_json_entry(written), other_build) == ConnectionState::Different);

    CHECK(compare_entry(read_json_entry(with_json_entry("", DEV)), DEV) == ConnectionState::Connected);
    CHECK_THROWS(with_json_entry("{broken", DEV));
    CHECK_THROWS(with_json_entry("[1,2]", DEV));
    CHECK_THROWS(with_json_entry(R"({"mcpServers":"oops"})", DEV));
    CHECK(json::parse(json_snippet(DEV))["mcpServers"]["bambu-studio"]["args"][2] == "/data/dev");
}

TEST_CASE("Codex TOML: the bambu-studio table only", "[agent_bridge][connect]")
{
    const std::string toml = R"(model = "gpt"
[mcp_servers.other]
command = "npx"
args = ["other"]

[mcp_servers.bambu-studio]
command = "/Apps/Studio Agent.app/Contents/MacOS/BambuStudio"
args = ["--mcp-stdio", "--datadir", "/data/dev"] # dev folder

[desktop]
args = ["not ours"]
)";
    const auto entry = read_toml_entry(toml);
    REQUIRE(entry);
    CHECK(*entry == DEV);
    CHECK_FALSE(read_toml_entry("[mcp_servers.other]\ncommand = \"x\"\n"));
    CHECK(read_toml_entry("[mcp_servers.\"bambu-studio\"]\ncommand = 'C:\\path'\n")->command == "C:\\path");
}

TEST_CASE("run_program collects output, exit code and times out", "[agent_bridge][connect]")
{
    const ProcessResult ok = run_program("/bin/sh", {"-c", "echo out; echo err >&2; exit 3"}, "/usr/bin:/bin");
    CHECK(ok.exit_code == 3);
    CHECK(ok.output.find("out") != std::string::npos);
    CHECK(ok.output.find("err") != std::string::npos);
    CHECK(run_program("/bin/sh", {"-c", "echo $PATH"}, "/custom/bin:/bin").output == "/custom/bin:/bin\n");

    const ProcessResult slow = run_program("/bin/sleep", {"10"}, "/usr/bin:/bin", std::chrono::milliseconds(300));
    CHECK(slow.timed_out);
    CHECK(slow.exit_code == -1);
    CHECK(run_program("/no/such/program", {}, "/bin").exit_code == -1);

    // Closes its output but keeps running: still ends at the deadline.
    const auto start = std::chrono::steady_clock::now();
    const ProcessResult quiet = run_program("/bin/sh", {"-c", "exec >&- 2>&-; sleep 10"}, "/usr/bin:/bin", std::chrono::milliseconds(400));
    CHECK(quiet.timed_out);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(3));

    // A timeout also ends what the tool started.
    const ProcessResult group = run_program("/bin/sh", {"-c", "sleep 10 & echo $!; wait"}, "/usr/bin:/bin", std::chrono::milliseconds(400));
    CHECK(group.timed_out);
    const int child = std::atoi(group.output.c_str());
    REQUIRE(child > 0);
    CHECK(::kill(child, 0) != 0);
}

TEST_CASE("A failed Claude Code reconnect puts the old entry back", "[agent_bridge][connect]")
{
    TempHome home;
    const AiApp claude = app_named("claude_code", home.str());
    // A Claude Code stand-in: refuses to add over an entry, and fails for the command "/fail".
    write(home.path / ".local" / "bin" / "claude", "#!/usr/bin/python3\n"
                                                   "import json, sys\n"
                                                   "path = '" + claude.config + "'\n"
                                                   "config = json.load(open(path))\n"
                                                   "servers = config.setdefault('mcpServers', {})\n"
                                                   "args = sys.argv[1:]\n"
                                                   "if args[1] == 'remove':\n"
                                                   "    servers.pop('bambu-studio')\n"
                                                   "else:\n"
                                                   "    rest = args[args.index('--') + 1:]\n"
                                                   "    if 'bambu-studio' in servers or rest[0] == '/fail':\n"
                                                   "        sys.exit(1)\n"
                                                   "    servers['bambu-studio'] = {'type': 'stdio', 'command': rest[0], 'args': rest[1:]}\n"
                                                   "json.dump(config, open(path, 'w'))\n");
    fs::permissions(home.path / ".local" / "bin" / "claude", fs::perms::owner_all);
    write(claude.config, R"({"projects":{},"mcpServers":{"bambu-studio":{"type":"stdio","command":"/old/BambuStudio","args":["--mcp-stdio"]}}})");

    const ConnectResult failed = connect_app(claude, McpEntry{"/fail", {"--mcp-stdio"}}, home.str());
    CHECK_FALSE(failed.ok);
    CHECK_FALSE(failed.changed);
    CHECK(failed.message.find("put back") != std::string::npos);
    CHECK(read_json_entry(read(claude.config))->command == "/old/BambuStudio");

    const ConnectResult ok = connect_app(claude, DEV, home.str());
    INFO(ok.message);
    CHECK(ok.ok);
    CHECK(app_status(claude, DEV, home.str()).state == ConnectionState::Connected);
}

TEST_CASE("Status, connecting through the app's CLI, and the startup check", "[agent_bridge][connect]")
{
    TempHome home;
    const AiApp codex = app_named("codex", home.str());

    // A Codex stand-in: `mcp add bambu-studio -- <command> <args...>` writes the table, recording calls.
    write(home.path / ".local" / "bin" / "codex", R"(#!/bin/sh
echo "$*" >> "$HOME_DIR/calls"
if [ "$2" = add ]; then
  shift 4
  cmd="$1"; shift
  args=""; for a in "$@"; do args="$args\"$a\", "; done
  printf '[mcp_servers.bambu-studio]\ncommand = "%s"\nargs = [%s]\n' "$cmd" "$args" > "$HOME_DIR/.codex/config.toml"
fi
)");
    // The script finds the home folder through its own path.
    const std::string script = read(home.path / ".local" / "bin" / "codex");
    write(home.path / ".local" / "bin" / "codex", "#!/bin/sh\nHOME_DIR='" + home.str() + "'\n" + script.substr(script.find('\n') + 1));
    fs::permissions(home.path / ".local" / "bin" / "codex", fs::perms::owner_all);
    write(home.path / ".codex" / "config.toml", "[mcp_servers.bambu-studio]\ncommand = \"/old/BambuStudio\"\nargs = [\"--mcp-stdio\"]\n");

    const AppStatus before = app_status(codex, DEV, home.str());
    CHECK(before.state == ConnectionState::Different);
    CHECK(before.found_command == "/old/BambuStudio");
    CHECK(before.cli_path == (home.path / ".local" / "bin" / "codex").string());

    const auto elsewhere = apps_pointing_elsewhere(DEV, home.str());
    REQUIRE(elsewhere.size() == 1);
    CHECK(elsewhere.front().id == "codex");

    const ConnectResult result = connect_app(codex, DEV, home.str());
    INFO(result.message);
    CHECK(result.ok);
    CHECK(fs::exists(result.backup));
    CHECK(read(result.backup).find("/old/BambuStudio") != std::string::npos);
    // codex mcp add updates in place: no remove step that could lose the entry.
    CHECK(read(home.path / "calls").find("mcp remove") == std::string::npos);
    CHECK(app_status(codex, DEV, home.str()).state == ConnectionState::Connected);
    CHECK(apps_pointing_elsewhere(DEV, home.str()).empty());

    // A second connect keeps the first backup and makes its own.
    const ConnectResult again = connect_app(codex, DEV, home.str());
    CHECK(again.ok);
    CHECK(again.backup != result.backup);
    CHECK(fs::exists(result.backup));

    // An unreadable Claude Desktop config is reported, not overwritten.
    const AiApp desktop = app_named("claude_desktop", home.str());
    write(desktop.config, "{not json");
    CHECK(app_status(desktop, DEV, home.str()).state == ConnectionState::Unreadable);
}
