// DeepSeek Harness patch file (cordis.patch.yml): reading and writing only the mcp-bambu-studio row,
// the YAML scalar forms Harness's yaml library writes, and connecting in a fake home folder. The
// fixtures under fixtures/cordis/ come from live-checks/harness-yaml-roundtrip.mjs (Harness's own yaml).
#include <catch2/catch.hpp>

#include <iterator>
#include <string>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include "slic3r/GUI/AgentBridge/AgentBridgeAiAppConfigsCordisYaml.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeAiAppConnect.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeYamlScalarCodec.hpp"

using namespace Slic3r::GUI::AgentBridge;
namespace fs = boost::filesystem;

namespace {
std::string read(const fs::path &path)
{
    boost::nowide::ifstream in(path.string(), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write(const fs::path &path, const std::string &text)
{
    fs::create_directories(path.parent_path());
    boost::nowide::ofstream(path.string(), std::ios::binary) << text;
}

std::string fixture(const std::string &name) { return read(fs::path(AGENT_BRIDGE_FIXTURES_DIR) / "cordis" / name); }

// The user's patch file before Studio touched it: the first rows of every fixture.
std::string user_rows() { return fixture("spaces-long-paths.studio-written.yml").substr(0, fixture("spaces-long-paths.studio-written.yml").find("\n# bambu-studio") + 1); }

const McpEntry SPACES{"/Applications/Bambu Studio Agent Build With A Rather Long Name.app/Contents/MacOS/BambuStudio",
                      {"--mcp-stdio", "--datadir", "/Users/vinh duong/Library/Application Support/BambuStudioAgent"}};
const McpEntry QUOTE{"/Users/o'neil/Apps/BambuStudio.app/Contents/MacOS/BambuStudio",
                     {"--mcp-stdio", "--datadir", "/Users/o'neil/Library/Application Support/BambuStudioAgent with a long folder name here"}};
const McpEntry DEV{"/dev/BambuStudio.app/Contents/MacOS/BambuStudio", {"--mcp-stdio", "--datadir", "/dev/data"}};

struct TempHome
{
    fs::path path = fs::temp_directory_path() / fs::unique_path("agent-cordis-%%%%-%%%%");
    TempHome() { fs::create_directories(path); }
    ~TempHome()
    {
        boost::system::error_code ec;
        fs::remove_all(path, ec);
    }
};

AiApp harness(const std::string &home)
{
    for (const AiApp &app : ai_apps(home))
        if (app.id == "deepseek_harness")
            return app;
    FAIL("no deepseek_harness app");
    return {};
}
} // namespace

TEST_CASE("YAML scalars as Harness's yaml library writes them", "[agent_bridge][connect][cordis]")
{
    CHECK(decode_yaml_scalar(" /a/b  # note") == std::string("/a/b"));
    CHECK(decode_yaml_scalar(" /a b\n      c d") == std::string("/a b c d"));      // folded plain
    CHECK(decode_yaml_scalar(" 'o''neil\n    x'") == std::string("o'neil x"));      // folded single-quoted
    CHECK(decode_yaml_scalar(" \"a\\\n   b\\tc\\u00e9\\\"\"") == std::string("ab\tc\xC3\xA9\"")); // escaped break joins
    CHECK(decode_yaml_scalar(" \"a\n\n   b\"") == std::string("a\nb"));             // empty line keeps a newline
    CHECK_FALSE(decode_yaml_scalar(" |\n  block"));
    CHECK_FALSE(decode_yaml_scalar(" *alias"));
    CHECK_FALSE(decode_yaml_scalar(" 'open"));

    CHECK(decode_yaml_flow_list(" [--mcp-stdio, '/a b',\n   \"c\", ]") == std::vector<std::string>{"--mcp-stdio", "/a b", "c"});
    CHECK(decode_yaml_flow_list(" []") == std::vector<std::string>{});
    CHECK_FALSE(decode_yaml_flow_list(" [a, [b]]"));

    CHECK(encode_yaml_scalar("--mcp-stdio") == "--mcp-stdio");
    CHECK(encode_yaml_scalar("/a/b.app") == "/a/b.app");
    CHECK(encode_yaml_scalar("/a b") == "'/a b'");
    CHECK(encode_yaml_scalar("123") == "'123'"); // not a number
    CHECK(encode_yaml_scalar("it's") == "'it''s'");
    CHECK(encode_yaml_scalar("a\nb") == "\"a\\nb\"");
    for (const std::string value : {"--mcp-stdio", "/a b", "it's", "a\nb\t\"\\", "123", ""})
        CHECK(decode_yaml_scalar(" " + encode_yaml_scalar(value)) == value);
}

TEST_CASE("Harness patch: Studio writes one insert row, the rest byte for byte", "[agent_bridge][connect][cordis]")
{
    const std::string user = user_rows();
    CHECK_FALSE(read_cordis_yaml_entry(user));

    // Exactly what Harness's yaml reads back as our entry (see the round-trip script).
    CHECK(with_cordis_yaml_entry(user, SPACES) == fixture("spaces-long-paths.studio-written.yml"));
    CHECK(with_cordis_yaml_entry(user, QUOTE) == fixture("quote-in-path.studio-written.yml"));

    // Connecting again changes nothing; connecting elsewhere replaces, never duplicates.
    const std::string once = with_cordis_yaml_entry(user, DEV);
    CHECK(once.rfind(user, 0) == 0);
    CHECK(with_cordis_yaml_entry(once, DEV) == once);
    CHECK(with_cordis_yaml_entry(with_cordis_yaml_entry(once, SPACES), DEV) == once);
    CHECK(read_cordis_yaml_entry(once) == DEV);

    // An empty patch is `[]` (comments only would fail Harness's boot): the placeholder makes way.
    CHECK(with_cordis_yaml_entry("# mine\n[]\n", DEV) == "# mine\n" + cordis_yaml_snippet(DEV) + "\n");
    CHECK(with_cordis_yaml_entry("", DEV) == cordis_yaml_snippet(DEV) + "\n");
    CHECK(read_cordis_yaml_entry("[]\n") == std::nullopt);

    // A row the user adds after ours stays; ours moves to the end.
    const std::string later = once + "- id: ui-chat\n  config:\n    transcriptView: compact\n";
    const std::string moved = with_cordis_yaml_entry(later, SPACES);
    CHECK(moved.find("transcriptView: compact") != std::string::npos);
    CHECK(moved.find("# bambu-studio") > moved.find("transcriptView: compact"));
    CHECK(read_cordis_yaml_entry(moved) == SPACES);
}

TEST_CASE("Harness patch: rows rewritten or overridden by Harness Settings", "[agent_bridge][connect][cordis]")
{
    for (const auto &[name, entry] : {std::pair{"spaces-long-paths", SPACES}, std::pair{"quote-in-path", QUOTE}}) {
        INFO(name);
        const std::string written = fixture(std::string(name) + ".studio-written.yml");
        CHECK(read_cordis_yaml_entry(written) == entry);
        // Settings changed another row: our scalars come back folded, still the same entry.
        CHECK(read_cordis_yaml_entry(fixture(std::string(name) + ".harness-rewritten.yml")) == entry);
        // Settings saved our row: a top-level override after the insert wins; connecting removes both.
        const std::string overridden = fixture(std::string(name) + ".harness-override.yml");
        CHECK(read_cordis_yaml_entry(overridden) == entry);
        CHECK(compare_entry(read_cordis_yaml_entry(overridden), DEV) == ConnectionState::Different);
        CHECK(with_cordis_yaml_entry(overridden, entry) == written);
    }
}

TEST_CASE("Harness patch: shapes Studio reads but leaves alone", "[agent_bridge][connect][cordis]")
{
    // Hand-made row with a flow list, inside an insert shared with another row: only ours goes.
    const std::string shared = "- insert:\n  - id: mcp-other\n    config: {a: 1}\n  - id: 'mcp-bambu-studio'\n    config:\n"
                               "      command: \"/x/BambuStudio\"\n      args: [--mcp-stdio, '/my data']\n- id: ui-chat\n  config: {}\n";
    CHECK(read_cordis_yaml_entry(shared) == McpEntry{"/x/BambuStudio", {"--mcp-stdio", "/my data"}});
    const std::string replaced = with_cordis_yaml_entry(shared, DEV);
    CHECK(replaced.rfind("- insert:\n  - id: mcp-other\n    config: {a: 1}\n- id: ui-chat\n  config: {}\n", 0) == 0);
    CHECK(read_cordis_yaml_entry(replaced) == DEV);

    // Our id with fields it cannot read: Different, never silently Connected.
    const auto odd = read_cordis_yaml_entry("- insert:\n    - id: mcp-bambu-studio\n      config:\n        command: !!js x\n        args: []\n");
    REQUIRE(odd);
    CHECK(odd->command.empty());

    // Flow insert holding our row: read as Different, not edited.
    const std::string flow_insert = "- insert: [{id: mcp-bambu-studio, config: {command: /x}}]\n";
    CHECK(read_cordis_yaml_entry(flow_insert)->command.empty());
    CHECK_THROWS(with_cordis_yaml_entry(flow_insert, DEV));

    // Not a top-level block list: nothing written.
    for (const std::string text : {"[{id: a}]\n", "id: a\n", "  - id: a\n"}) {
        INFO(text);
        CHECK_THROWS(read_cordis_yaml_entry(text));
        CHECK_THROWS(with_cordis_yaml_entry(text, DEV));
    }
}

TEST_CASE("Connecting DeepSeek Harness in a fake home folder", "[agent_bridge][connect][cordis]")
{
    TempHome   home;
    const auto app = harness(home.path.string());
    CHECK(app.cli.empty());
    CHECK(app.format == ConfigFormat::CordisYaml);
    const fs::path config = app.config;

    // The profile folder alone shows Harness is installed; no file yet: a new private file.
    fs::create_directories(config.parent_path());
    CHECK(app_status(app, DEV, home.path.string()).state == ConnectionState::NotConnected);
    ConnectResult result = connect_app(app, DEV, home.path.string());
    INFO(result.message);
    REQUIRE(result.ok);
    CHECK(result.backup.empty());
    CHECK((fs::status(config).permissions() & fs::all_all) == (fs::owner_read | fs::owner_write));
    CHECK(read(config) == cordis_yaml_snippet(DEV) + "\n");

    // Pointing elsewhere: the startup offer lists it; connecting backs up, keeps the user's rows and mode.
    write(config, user_rows() + "\n" + cordis_yaml_snippet(SPACES) + "\n");
    fs::permissions(config, fs::owner_read | fs::owner_write);
    CHECK(app_status(app, DEV, home.path.string()).state == ConnectionState::Different);
    bool listed = false;
    for (const AiApp &elsewhere : apps_pointing_elsewhere(DEV, home.path.string()))
        listed = listed || elsewhere.id == "deepseek_harness";
    CHECK(listed);
    result = connect_app(app, DEV, home.path.string());
    REQUIRE(result.ok);
    CHECK(read(result.backup).find("Rather Long Name") != std::string::npos);
    CHECK(read(config) == user_rows() + cordis_yaml_snippet(DEV) + "\n"); // user_rows() ends with the one blank line
    CHECK((fs::status(config).permissions() & fs::all_all) == (fs::owner_read | fs::owner_write));
    CHECK(app_status(app, DEV, home.path.string()).state == ConnectionState::Connected);

    // A file Studio cannot edit: Unreadable, and connecting leaves it as it was.
    write(config, "[{id: a}]\n");
    CHECK(app_status(app, DEV, home.path.string()).state == ConnectionState::Unreadable);
    CHECK_FALSE(connect_app(app, DEV, home.path.string()).ok);
    CHECK(read(config) == "[{id: a}]\n");
}

TEST_CASE("Harness patch: review edge cases", "[agent_bridge][connect][cordis]")
{
    // CRLF file: read as Connected, our lines written with the file's line ending.
    std::string crlf = cordis_yaml_snippet(DEV) + "\n";
    for (size_t at = crlf.find('\n'); at != std::string::npos; at = crlf.find('\n', at + 2))
        crlf.insert(at, "\r");
    CHECK(read_cordis_yaml_entry(crlf) == DEV);
    CHECK(with_cordis_yaml_entry(crlf, DEV) == crlf);

    // A key beside the insert list is not part of our row; with our row alone, Studio will not edit.
    const std::string sibling = "- insert:\n    - id: mcp-bambu-studio\n      config:\n        command: /x\n        args: []\n  after: ui-chat\n";
    CHECK(read_cordis_yaml_entry(sibling) == McpEntry{"/x", {}});
    CHECK_THROWS(with_cordis_yaml_entry(sibling, DEV));
    const std::string beside = "- insert:\n    - id: mcp-other\n    - id: mcp-bambu-studio\n  after: ui-chat\n";
    CHECK(with_cordis_yaml_entry(beside, DEV).rfind("- insert:\n    - id: mcp-other\n  after: ui-chat\n", 0) == 0);

    // Two rows of ours in one insert: the whole insert goes, no bare `- insert:` left.
    const std::string twice = "- id: ui-chat\n- insert:\n    - id: mcp-bambu-studio\n    - id: \"mcp-bambu-studio\"  # again\n";
    CHECK(with_cordis_yaml_entry(twice, DEV) == "- id: ui-chat\n\n" + cordis_yaml_snippet(DEV) + "\n");

    // `#` without a space is part of the id in YAML: another row, kept.
    const std::string other = "- id: mcp-bambu-studio#keep\n  config: {}\n";
    CHECK_FALSE(read_cordis_yaml_entry(other));
    CHECK(with_cordis_yaml_entry(other, DEV).rfind(other, 0) == 0);

    // A bare `-` with deeper keys is still ours: reconnect replaces it.
    const std::string bare = "- insert:\n    -\n        id: mcp-bambu-studio\n        config:\n          command: /x\n          args: []\n";
    CHECK(read_cordis_yaml_entry(bare) == McpEntry{"/x", {}});
    CHECK(with_cordis_yaml_entry(bare, DEV) == cordis_yaml_snippet(DEV) + "\n");
}
