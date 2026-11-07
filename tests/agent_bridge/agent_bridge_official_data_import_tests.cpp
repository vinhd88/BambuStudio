// Import from the official Studio's data folder into the agent build's own folder, on a fixture
// folder that holds the kinds of secrets the import must leave behind.
#include <catch2/catch.hpp>

#include <iterator>
#include <map>
#include <string>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include "slic3r/GUI/AgentBridge/AgentBridgeAgentDataFolder.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeOfficialDataImport.hpp"

using namespace Slic3r::GUI::AgentBridge;
using nlohmann::json;
namespace fs = boost::filesystem;

namespace {
const char *const ACCOUNT = "3141592653";

struct TempFolder
{
    fs::path path = fs::temp_directory_path() / fs::unique_path("agent-import-%%%%-%%%%");
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
    boost::nowide::ofstream(path.string()) << text;
}

std::string read(const fs::path &path)
{
    boost::nowide::ifstream in(path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Every file and its content: the official folder must look the same after an import.
std::map<std::string, std::string> snapshot(const fs::path &root)
{
    std::map<std::string, std::string> files;
    for (fs::recursive_directory_iterator it(root), end; it != end; ++it)
        if (fs::is_regular_file(it->symlink_status()))
            files[fs::relative(it->path(), root).string()] = read(it->path());
    return files;
}

std::string info(const std::string &setting_id)
{
    return std::string("sync_info = update\nuser_id = ") + ACCOUNT + "\nsetting_id = " + setting_id + "\nbase_id = GFSG99_03\nupdated_time = 25199\n";
}

void make_official(const fs::path &dir)
{
    json conf = {{"header", "BambuStudio"},
                 {"app",
                  {{"version", "02.03.00.70"}, {"language", "en_GB"}, {"region", "Europe"}, {"curr_bed_type", "4"}, {"sync_user_preset", true},
                   {"user_id", ACCOUNT}, {"slicer_uuid", "uuid-secret"}, {"ip_address", "192.168.1.50"}}},
                 {"presets", {{"machine", "My A1"}, {"process", "0.20mm Standard @BBL A1"}, {"filaments", {"My PETG"}}}},
                 {"models", json::array({{{"vendor", "BBL"}, {"model", "Bambu Lab A1"}}})},
                 {"filaments", json::array({"Bambu PLA Basic @BBL A1"})},
                 {"firstguide", {{"finish", true}, {"privacyuse", true}}},
                 {"access_code", {{"03919A000000001", "12345678"}}},
                 {"user_access_code", {{"03919A000000001", "87654321"}}},
                 {"recent_projects", {{"001", "/Users/someone/secret-project.3mf"}}}};
    write(dir / "BambuStudio.conf", conf.dump(4));
    write(dir / "BambuNetworkEngine.conf", "{\"token\":\"login-secret\"}");
    write(dir / "plugins" / "libbambu_networking.dylib", "binary");
    write(dir / "log" / "studio.log", "log");
    write(dir / "certs" / "slicer.pem", "cert");
    write(dir / "system" / "BBL.json", "{\"version\":\"02.03\"}");
    write(dir / "system" / "BBL" / "machine" / "Bambu Lab A1.json", "{}");
    write(dir / "printers" / "N2S.json", "{}");
    write(dir / "user" / ACCOUNT / "filament" / "My PETG.json", "{\"name\":\"My PETG\",\"from\":\"User\"}");
    write(dir / "user" / ACCOUNT / "filament" / "My PETG.info", info("PFUS1"));
    write(dir / "user" / ACCOUNT / "machine" / "My A1.json", "{\"name\":\"My A1\",\"from\":\"User\"}");
    write(dir / "user" / ACCOUNT / "machine" / "My A1.info", info("PMUS1"));
    write(dir / "user" / "default" / "process" / "Local process.json", "{\"name\":\"Local process\"}");
}

bool contains(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }
} // namespace

TEST_CASE("First import copies presets and selections, nothing secret", "[agent_bridge][import]")
{
    TempFolder official, agent;
    make_official(official.path);
    const auto before = snapshot(official.path);

    const ImportSummary summary = import_official_data(official.path, agent.path, false);
    CHECK(summary.source_found);
    CHECK(summary.config_copied);
    CHECK(summary.user_presets == 3);
    CHECK(summary.problems.empty());
    CHECK(snapshot(official.path) == before);

    CHECK(fs::exists(agent.path / "system" / "BBL" / "machine" / "Bambu Lab A1.json"));
    CHECK(fs::exists(agent.path / "printers" / "N2S.json"));
    const fs::path presets = agent.path / "user" / "default";
    CHECK(fs::exists(presets / "filament" / "My PETG.json"));
    CHECK(fs::exists(presets / "machine" / "My A1.json"));
    CHECK(fs::exists(presets / "process" / "Local process.json"));
    CHECK_FALSE(fs::exists(agent.path / "user" / ACCOUNT));
    const std::string petg_info = read(presets / "filament" / "My PETG.info");
    CHECK(contains(petg_info, "user_id = \n"));
    CHECK(contains(petg_info, "sync_info = \n"));
    CHECK(contains(petg_info, "setting_id = PFUS1"));

    for (const char *left_behind : {"plugins", "log", "certs", "BambuNetworkEngine.conf"})
        CHECK_FALSE(fs::exists(agent.path / left_behind));
    // Nothing anywhere in the agent folder names the account or a secret.
    for (const auto &[file, text] : snapshot(agent.path))
        for (const char *secret : {ACCOUNT, "12345678", "87654321", "uuid-secret", "192.168.1.50", "login-secret", "secret-project"}) {
            INFO(file << " contains " << secret);
            CHECK_FALSE(contains(text, secret));
        }

    const json conf = json::parse(read(agent.path / "BambuStudio.conf"));
    CHECK(conf["presets"]["machine"] == "My A1");
    CHECK(conf["models"][0]["model"] == "Bambu Lab A1");
    CHECK(conf["filaments"][0] == "Bambu PLA Basic @BBL A1");
    CHECK(conf["app"]["version"] == "02.03.00.70");
    CHECK(conf["app"]["language"] == "en_GB");
    CHECK(conf["app"]["sync_user_preset"] == false);
    CHECK(conf["firstguide"]["finish"] == true);
    CHECK_FALSE(conf["firstguide"].contains("privacyuse"));
    for (const char *section : {"access_code", "user_access_code", "recent_projects", "header"})
        CHECK_FALSE(conf.contains(section));
}

TEST_CASE("Re-import replaces same-name presets and keeps the agent's own", "[agent_bridge][import]")
{
    TempFolder official, agent;
    make_official(official.path);
    write(agent.path / "user" / "default" / "filament" / "My PETG.json", "{\"name\":\"My PETG\",\"edited\":\"in agent build\"}");
    write(agent.path / "user" / "default" / "filament" / "Agent only.json", "{\"name\":\"Agent only\"}");
    write(agent.path / "BambuStudio.conf", json({{"app", {{"agent_bridge_enabled", false}, {"language", "de_DE"}}}, {"presets", {{"machine", "Old"}}}}).dump());

    const ImportSummary summary = import_official_data(official.path, agent.path, true);
    CHECK(summary.problems.empty());
    CHECK_FALSE(contains(read(agent.path / "user" / "default" / "filament" / "My PETG.json"), "in agent build"));
    CHECK(fs::exists(agent.path / "user" / "default" / "filament" / "Agent only.json"));
    const json conf = json::parse(read(agent.path / "BambuStudio.conf"));
    CHECK(conf["app"]["agent_bridge_enabled"] == false);
    CHECK(conf["app"]["language"] == "en_GB");
    CHECK(conf["presets"]["machine"] == "My A1");
}

TEST_CASE("Import without official data or with a broken config", "[agent_bridge][import]")
{
    TempFolder official, agent;
    const ImportSummary none = import_official_data(official.path / "missing", agent.path, false);
    CHECK_FALSE(none.source_found);
    CHECK(fs::is_empty(agent.path));

    make_official(official.path);
    write(official.path / "BambuStudio.conf", "{\"app\": {\"version\"");
    const ImportSummary broken = import_official_data(official.path, agent.path, false);
    CHECK(broken.user_presets == 3);
    CHECK_FALSE(broken.config_copied);
    REQUIRE(broken.problems.size() == 1);
    CHECK(contains(broken.problems.front(), "preset selections"));
    // No config: the next start tries again.
    CHECK_FALSE(fs::exists(agent.path / "BambuStudio.conf"));
    CHECK_FALSE(fs::exists(agent.path / "BambuStudio.conf.importing"));
}

TEST_CASE("Import does not follow links out of the official folder", "[agent_bridge][import]")
{
    TempFolder official, agent, outside;
    make_official(official.path);
    write(outside.path / "private" / "notes.json", "outside");
    fs::create_directory_symlink(outside.path / "private", official.path / "user" / ACCOUNT / "linked");
    fs::create_symlink(outside.path / "private" / "notes.json", official.path / "system" / "linked.json");

    import_official_data(official.path, agent.path, false);
    for (const auto &[file, text] : snapshot(agent.path))
        CHECK(text != "outside");
}

TEST_CASE("Import never writes into the official folder through a link in the agent folder", "[agent_bridge][import]")
{
    TempFolder official, agent;
    make_official(official.path);
    const auto before = snapshot(official.path);
    fs::create_directories(agent.path / "user");
    // A user sharing presets between the two apps.
    fs::create_directory_symlink(official.path / "user" / "default", agent.path / "user" / "default");

    const ImportSummary summary = import_official_data(official.path, agent.path, true);
    CHECK_FALSE(summary.problems.empty());
    CHECK(snapshot(official.path) == before);
}

TEST_CASE("Agent data folder: first run, then re-import on request", "[agent_bridge][import]")
{
    CHECK(agent_data_dir_for("/Users/me/Library/Application Support/BambuStudio") == "/Users/me/Library/Application Support/BambuStudioAgent");
    CHECK(agent_data_dir_for("/Users/me/Library/Application Support/BambuStudio/") == "/Users/me/Library/Application Support/BambuStudioAgent");

    TempFolder official, parent;
    make_official(official.path);
    const fs::path agent = parent.path / "BambuStudioAgent";

    // --datadir run on a new folder: no automatic import.
    prepare_agent_data_folder(official.path.string(), agent.string(), false);
    CHECK_FALSE(take_startup_import_summary());

    prepare_agent_data_folder(official.path.string(), agent.string(), true);
    const auto first = take_startup_import_summary();
    REQUIRE(first);
    CHECK(first->config_copied);
    CHECK_FALSE(take_startup_import_summary());

    // Second start: the folder has its config, nothing runs.
    prepare_agent_data_folder(official.path.string(), agent.string(), true);
    CHECK_FALSE(take_startup_import_summary());

    // Asked for: the mark only. The backup is made at the next start, just before importing.
    write(agent / "user" / "default" / "process" / "Saved after asking.json", "{\"name\":\"Saved after asking\"}");
    schedule_reimport(agent.string());
    CHECK(fs::exists(agent / "agent-reimport-pending"));
    schedule_reimport(agent.string()); // a second click changes nothing

    write(official.path / "user" / ACCOUNT / "process" / "New.json", "{\"name\":\"New\"}");
    prepare_agent_data_folder(official.path.string(), agent.string(), false);
    const auto again = take_startup_import_summary();
    REQUIRE(again);
    CHECK(again->problems.empty());
    CHECK(again->user_presets == 4);
    CHECK(fs::exists(agent / "user" / "default" / "process" / "New.json"));
    CHECK(fs::exists(agent / "user" / "default" / "process" / "Saved after asking.json"));
    CHECK_FALSE(fs::exists(agent / "agent-reimport-pending"));

    // The backup: presets and app config only, including edits saved after asking.
    const fs::path backup = again->backup;
    CHECK(backup.parent_path() == parent.path);
    CHECK(fs::exists(backup / "BambuStudio.conf"));
    CHECK(fs::exists(backup / "user" / "default" / "process" / "Saved after asking.json"));
    CHECK_FALSE(fs::exists(backup / "user" / "default" / "process" / "New.json"));
    CHECK_FALSE(fs::exists(backup / "system"));
    std::size_t backups = 0;
    for (const fs::directory_entry &entry : fs::directory_iterator(parent.path))
        backups += entry.path().filename().string().rfind("BambuStudioAgent-backup-", 0) == 0;
    CHECK(backups == 1);

    // Never imports into the official folder itself.
    write(official.path / "agent-reimport-pending", "");
    prepare_agent_data_folder(official.path.string(), official.path.string(), true);
    CHECK_FALSE(take_startup_import_summary());
}
