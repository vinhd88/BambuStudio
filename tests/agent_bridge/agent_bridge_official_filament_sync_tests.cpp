// Filament sync from the official Studio: reading its filament slots, and copying its user filament
// presets into the agent folder, on fixture folders.
#include <catch2/catch.hpp>

#include <algorithm>
#include <iterator>
#include <map>
#include <string>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include "slic3r/GUI/AgentBridge/AgentBridgeOfficialFilamentSync.hpp"

using namespace Slic3r::GUI::AgentBridge;
using nlohmann::json;
namespace fs = boost::filesystem;

namespace {
const char *const ACCOUNT = "3141592653";

struct TempFolder
{
    fs::path path = fs::temp_directory_path() / fs::unique_path("agent-filament-sync-%%%%-%%%%");
    TempFolder() { fs::create_directories(path); }
    ~TempFolder()
    {
        boost::system::error_code ec;
        fs::remove_all(path, ec);
    }
};

void write(const fs::path &path, const std::string &text)
{
    fs::create_directories(path.parent_path());
    boost::nowide::ofstream(path.string(), std::ios::binary) << text;
}

std::string read(const fs::path &path)
{
    boost::nowide::ifstream in(path.string(), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::map<std::string, std::string> snapshot(const fs::path &root)
{
    std::map<std::string, std::string> files;
    for (fs::recursive_directory_iterator it(root), end; it != end; ++it)
        if (fs::is_regular_file(it->symlink_status()))
            files[fs::relative(it->path(), root).string()] = read(it->path());
    return files;
}

void write_conf(const fs::path &official, const json &presets) { write(official / "BambuStudio.conf", json{{"presets", presets}}.dump(4)); }

std::string info() { return std::string("sync_info = update\nuser_id = ") + ACCOUNT + "\nsetting_id = PFUS1\n"; }

bool contains(const std::vector<std::string> &names, const std::string &name) { return std::find(names.begin(), names.end(), name) != names.end(); }
} // namespace

TEST_CASE("Filament slots are read from the official settings", "[AgentBridge][FilamentSync]")
{
    TempFolder official;
    write_conf(official.path, {{"machine", "Bambu Lab A1 0.4 nozzle"},
                               {"filaments", {"Bambu PETG Basic @BBL A1", "Bambu PETG Basic @BBL A1", "Bambu PLA Basic @BBL A1"}},
                               {"filament_colors", "#FCE300,#000000"},
                               {"filament_is_mixed", "0,0,1"}});
    const OfficialFilamentSlots slots = read_official_filament_slots(official.path);
    CHECK(slots.printer == "Bambu Lab A1 0.4 nozzle");
    REQUIRE(slots.presets.size() == 3);
    CHECK(slots.presets[0] == "Bambu PETG Basic @BBL A1");
    // Fewer colours than slots: the rest are empty, not shifted.
    CHECK(slots.colours == std::vector<std::string>{"#FCE300", "#000000", ""});
    // No multi colours saved: same as the colours.
    CHECK(slots.multi_colours == slots.colours);
    CHECK(slots.mixed == std::vector<bool>{false, false, true});
}

TEST_CASE("Unreadable official settings give a message for the user", "[AgentBridge][FilamentSync]")
{
    TempFolder official;
    SECTION("missing")
    {
        CHECK_THROWS_WITH(read_official_filament_slots(official.path), Catch::Contains("were not found"));
    }
    SECTION("not JSON")
    {
        write(official.path / "BambuStudio.conf", "{ half written");
        CHECK_THROWS_WITH(read_official_filament_slots(official.path), Catch::Contains("could not be read"));
    }
    SECTION("no filament list")
    {
        write_conf(official.path, {{"machine", "Bambu Lab A1 0.4 nozzle"}});
        CHECK_THROWS_WITH(read_official_filament_slots(official.path), Catch::Contains("no filament list"));
    }
}

TEST_CASE("User filament presets are copied, and only those", "[AgentBridge][FilamentSync]")
{
    TempFolder official, agent_parent;
    const fs::path agent = agent_parent.path / "agent";
    const fs::path account = official.path / "user" / ACCOUNT, fallback = official.path / "user" / "default";
    write(account / "filament" / "My PETG.json", R"({"name": "My PETG", "from": "account"})");
    write(account / "filament" / "My PETG.info", info());
    write(fallback / "filament" / "My PETG.json", R"({"name": "My PETG", "from": "default"})");
    write(fallback / "filament" / "Old PLA.json", R"({"name": "Old PLA"})");
    write(account / "process" / "My Process.json", "{}");
    write(account / "machine" / "My Printer.json", "{}");
    const auto official_before = snapshot(official.path);

    SECTION("dry run writes nothing")
    {
        const FilamentPresetCopy copy = copy_official_filament_presets(official.path, agent, true);
        CHECK(copy.added.size() == 2);
        CHECK(copy.problems.empty());
        CHECK_FALSE(fs::exists(agent));
    }
    SECTION("copy")
    {
        const FilamentPresetCopy copy = copy_official_filament_presets(official.path, agent, false);
        CHECK(copy.problems.empty());
        CHECK(contains(copy.added, "My PETG"));
        CHECK(contains(copy.added, "Old PLA"));
        const fs::path target = agent / "user" / "default" / "filament";
        // The account's preset wins over default's.
        CHECK(read(target / "My PETG.json").find("account") != std::string::npos);
        const std::string copied_info = read(target / "My PETG.info");
        CHECK(copied_info.find(ACCOUNT) == std::string::npos);
        CHECK(copied_info.find("user_id = \n") != std::string::npos);
        CHECK_FALSE(fs::exists(agent / "user" / "default" / "process"));
        CHECK_FALSE(fs::exists(agent / "user" / "default" / "machine"));
        CHECK(copy.backup.empty());

        SECTION("the same again changes nothing")
        {
            const FilamentPresetCopy again = copy_official_filament_presets(official.path, agent, false);
            CHECK(again.added.empty());
            CHECK(again.updated.empty());
            CHECK(again.unchanged.size() == 2);
            CHECK(again.backup.empty());
        }
        SECTION("a changed preset is backed up, then replaced")
        {
            write(target / "Old PLA.json", R"({"name": "Old PLA", "edited": "in agent"})");
            write(target / "Agent Only.json", "{}");
            const FilamentPresetCopy again = copy_official_filament_presets(official.path, agent, false);
            CHECK(again.updated == std::vector<std::string>{"Old PLA"});
            REQUIRE_FALSE(again.backup.empty());
            CHECK(read(fs::path(again.backup) / "user" / "default" / "filament" / "Old PLA.json").find("in agent") != std::string::npos);
            CHECK(read(target / "Old PLA.json") == R"({"name": "Old PLA"})");
            // Presets only the agent has stay.
            CHECK(fs::exists(target / "Agent Only.json"));
            fs::remove_all(again.backup);
        }
    }
    CHECK(snapshot(official.path) == official_before);
}

TEST_CASE("A linked account folder is not followed", "[AgentBridge][FilamentSync]")
{
    TempFolder official, elsewhere, agent_parent;
    write(elsewhere.path / "filament" / "Foreign.json", "{}");
    fs::create_directories(official.path / "user");
    fs::create_directory_symlink(elsewhere.path, official.path / "user" / ACCOUNT);
    const FilamentPresetCopy copy = copy_official_filament_presets(official.path, agent_parent.path / "agent", false);
    CHECK(copy.added.empty());
    CHECK(copy.problems.empty());
}

TEST_CASE("No official folder is reported, not thrown", "[AgentBridge][FilamentSync]")
{
    TempFolder agent_parent;
    const FilamentPresetCopy copy = copy_official_filament_presets(agent_parent.path / "missing", agent_parent.path / "agent", false);
    CHECK(copy.added.empty());
    CHECK(copy.problems.size() == 1);
}

TEST_CASE("A preset's .json and .info come from the same folder", "[AgentBridge][FilamentSync]")
{
    TempFolder official, agent_parent;
    const fs::path agent = agent_parent.path / "agent";
    // The account has the preset without an .info; default has both.
    write(official.path / "user" / ACCOUNT / "filament" / "My PETG.json", R"({"from": "account"})");
    write(official.path / "user" / "default" / "filament" / "My PETG.json", R"({"from": "default"})");
    write(official.path / "user" / "default" / "filament" / "My PETG.info", info());
    const FilamentPresetCopy copy = copy_official_filament_presets(official.path, agent, false);
    CHECK(copy.problems.empty());
    const fs::path target = agent / "user" / "default" / "filament";
    CHECK(read(target / "My PETG.json").find("account") != std::string::npos);
    CHECK_FALSE(fs::exists(target / "My PETG.info"));
}

TEST_CASE("An agent folder linked into the official one is refused", "[AgentBridge][FilamentSync]")
{
    TempFolder official, agent_parent;
    const fs::path agent = agent_parent.path / "agent";
    write(official.path / "user" / ACCOUNT / "filament" / "My PETG.json", "{}");
    fs::create_directories(agent / "user" / "default");
    fs::create_directory_symlink(official.path / "user" / ACCOUNT / "filament", agent / "user" / "default" / "filament");
    const auto before = snapshot(official.path);
    for (bool dry_run : {true, false}) {
        const FilamentPresetCopy copy = copy_official_filament_presets(official.path, agent, dry_run);
        CHECK(copy.added.empty());
        CHECK(copy.problems.size() == 1);
    }
    CHECK(snapshot(official.path) == before);
}

TEST_CASE("A link left at the temporary file is not written through", "[AgentBridge][FilamentSync]")
{
    TempFolder official, agent_parent;
    const fs::path agent  = agent_parent.path / "agent";
    const fs::path target = agent / "user" / "default" / "filament";
    write(official.path / "user" / ACCOUNT / "filament" / "My PETG.info", info());
    write(official.path / "user" / ACCOUNT / "filament" / "My PETG.json", "{}");
    write(official.path / "victim.txt", "official");
    fs::create_directories(target);
    fs::create_symlink(official.path / "victim.txt", target / "My PETG.info.importing");
    const FilamentPresetCopy copy = copy_official_filament_presets(official.path, agent, false);
    CHECK(copy.problems.empty());
    CHECK(read(official.path / "victim.txt") == "official");
    CHECK(read(target / "My PETG.info").find("user_id = \n") != std::string::npos);
}
