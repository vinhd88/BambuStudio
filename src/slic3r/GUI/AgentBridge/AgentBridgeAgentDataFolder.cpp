#include "AgentBridgeAgentDataFolder.hpp"

#include <cstdlib>
#include <ctime>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

namespace fs = boost::filesystem;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
const char *const REIMPORT_MARKER = "agent-reimport-pending";
// What a re-import can overwrite. Vendor profiles (system, printers) come back from the official app.
const char *const BACKED_UP[] = {"user", "BambuStudio.conf"};

std::optional<ImportSummary> s_startup_summary;

bool same_folder(const fs::path &a, const fs::path &b)
{
    boost::system::error_code ec;
    return fs::equivalent(a, b, ec) || a.lexically_normal() == b.lexically_normal();
}

std::string timestamp()
{
    const std::time_t now = std::time(nullptr);
    std::tm           local{};
    localtime_r(&now, &local);
    char text[32];
    std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &local);
    return text;
}

// A name nothing has used yet: backups are never overwritten.
fs::path new_backup_path(const fs::path &agent_dir)
{
    const std::string base = agent_dir.lexically_normal().string() + "-backup-" + timestamp();
    fs::path          path = base;
    for (int n = 2; fs::exists(path); ++n)
        path = base + "-" + std::to_string(n);
    return path;
}

// Copies the user presets and app config to a new "<agent>-backup-<time>" beside the folder. Removes
// a half-made backup on failure. Links are not followed.
std::string back_up_presets(const fs::path &agent)
{
    const fs::path backup = new_backup_path(agent);
    try {
        fs::create_directories(backup);
        for (const char *name : BACKED_UP) {
            const fs::path from = agent / name;
            if (fs::is_regular_file(fs::symlink_status(from)))
                fs::copy_file(from, backup / name);
            else if (fs::is_directory(fs::symlink_status(from)))
                for (fs::recursive_directory_iterator it(from), end; it != end; ++it) {
                    const fs::path to = backup / name / it->path().lexically_relative(from);
                    if (fs::is_directory(it->symlink_status()))
                        fs::create_directories(to);
                    else if (fs::is_regular_file(it->symlink_status()))
                        fs::copy_file(it->path(), to);
                }
        }
    } catch (...) {
        boost::system::error_code ec;
        fs::remove_all(backup, ec);
        throw;
    }
    return backup.string();
}
} // namespace

std::string agent_data_dir_for(const std::string &official_dir)
{
    fs::path official = fs::path(official_dir).lexically_normal();
    if (official.filename_is_dot())
        official = official.parent_path();
    return official.string() + "Agent";
}

std::string official_data_dir_from_home()
{
    const char *home = std::getenv("HOME");
    if (home == nullptr || *home == '\0')
        return "";
    return (fs::path(home) / "Library" / "Application Support" / "BambuStudio").string();
}

void prepare_agent_data_folder(const std::string &official_dir, const std::string &agent_dir, bool import_if_new)
{
    const fs::path agent(agent_dir), marker = agent / REIMPORT_MARKER;
    boost::system::error_code ec;
    try {
        if (official_dir.empty() || same_folder(official_dir, agent))
            return;
        const bool asked  = fs::exists(marker);
        const bool is_new = import_if_new && !fs::exists(agent / "BambuStudio.conf");
        if (!asked && !is_new)
            return;
        // Asked once, done once, even if it fails: the notice says what happened.
        fs::remove(marker, ec);
        fs::create_directories(agent);
        // Backed up now, not when asked: edits saved since then are kept too.
        std::string backup;
        if (!is_new)
            backup = back_up_presets(agent);
        s_startup_summary        = import_official_data(official_dir, agent, /*reimport*/ !is_new);
        s_startup_summary->backup = backup;
    } catch (const std::exception &e) {
        fs::remove(marker, ec);
        ImportSummary failed;
        failed.source_found = true;
        failed.problems.push_back(std::string("nothing imported: ") + e.what());
        s_startup_summary = failed;
    }
}

std::optional<ImportSummary> take_startup_import_summary()
{
    std::optional<ImportSummary> summary;
    summary.swap(s_startup_summary);
    return summary;
}

void schedule_reimport(const std::string &agent_dir)
{
    boost::nowide::ofstream marker((fs::path(agent_dir) / REIMPORT_MARKER).string());
    marker << "Re-import from the official Studio at the next start.\n";
    if (!marker.flush())
        throw std::runtime_error("cannot mark the re-import in " + agent_dir);
}

}}} // namespace Slic3r::GUI::AgentBridge
