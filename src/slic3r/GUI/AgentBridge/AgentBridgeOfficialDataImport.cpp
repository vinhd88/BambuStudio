#include "AgentBridgeOfficialDataImport.hpp"

#include <algorithm>
#include <functional>
#include <iterator>
#include <sstream>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

namespace fs = boost::filesystem;
using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
const char *const APP_CONFIG = "BambuStudio.conf";

// Allow-lists, not deny-lists: anything Studio adds later (logins, devices, cloud state) stays behind.
// Not copied, among others: plugins, log, cache, hms, track, certs, ota, BambuNetworkEngine.conf, and
// config entries with access codes, printer addresses, account and recent projects.
const char *const PROFILE_FOLDERS[] = {"system", "printers"};
// Preset selections, installed printer models and filaments: copied whole.
const char *const CONFIG_SECTIONS[] = {"presets", "models", "filaments"};
const std::pair<const char *, const char *> CONFIG_KEYS[] = {
    {"app", "version"}, {"app", "language"}, {"app", "region"}, {"app", "curr_bed_type"}, {"firstguide", "finish"}};

std::string read_file(const fs::path &path)
{
    boost::nowide::ifstream in(path.string(), std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + path.filename().string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Refuses a write that would land in the official folder, for example through a link the user made
// from the agent folder into it. Resolves links in the target's folder; the folder must exist.
void check_outside(const fs::path &target, const fs::path &official)
{
    const fs::path folder = fs::weakly_canonical(target.parent_path());
    const fs::path inside = folder.lexically_relative(official);
    if (!inside.empty() && *inside.begin() != "..")
        throw std::runtime_error("refused to write into the official Studio folder: " + target.filename().string());
}

// Written beside the target and renamed into place, so a crash never leaves half a file.
void write_file(const fs::path &path, const std::string &text)
{
    const fs::path temp = path.string() + ".importing";
    {
        boost::nowide::ofstream out(temp.string(), std::ios::binary | std::ios::trunc);
        out << text;
        if (!out.flush())
            throw std::runtime_error("cannot write " + path.filename().string());
    }
    fs::rename(temp, path);
}

// Regular files under `from`, as paths relative to it. Links inside are skipped and not entered;
// callers skip `from` itself if it is a link.
std::vector<fs::path> files_under(const fs::path &from)
{
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(from), end; it != end; ++it)
        if (fs::is_regular_file(it->symlink_status()))
            files.push_back(it->path().lexically_relative(from));
    std::sort(files.begin(), files.end());
    return files;
}

void copy_tree(const fs::path &from, const fs::path &to, const fs::path &official)
{
    for (const fs::path &file : files_under(from)) {
        fs::create_directories((to / file).parent_path());
        check_outside(to / file, official);
        fs::copy_file(from / file, to / file, fs::copy_options::overwrite_existing);
    }
}

// A preset's .info file without the account it was synced with: the agent build is logged out.
std::string without_account(const std::string &info)
{
    std::istringstream in(info);
    std::string        out;
    for (std::string line; std::getline(in, line);) {
        const std::string key = line.substr(0, line.find_first_of(" ="));
        if (key == "user_id" || key == "sync_info")
            line = key + " = ";
        out += line + '\n';
    }
    return out;
}

// Presets of every account folder, and of `default`, merged into user/default. `default` goes
// first, so the account's presets win a name clash: they are the ones the user sees in Studio.
int copy_user_presets(const fs::path &official_user, const fs::path &agent_default, const fs::path &official)
{
    std::vector<fs::path> sources;
    for (const fs::directory_entry &entry : fs::directory_iterator(official_user))
        if (fs::is_directory(entry.symlink_status())) // a link to a folder is skipped
            sources.push_back(entry.path());
    std::sort(sources.begin(), sources.end(), [](const fs::path &a, const fs::path &b) {
        return (a.filename() == "default") != (b.filename() == "default") ? a.filename() == "default" : a < b;
    });

    int presets = 0;
    for (const fs::path &source : sources)
        for (const fs::path &file : files_under(source)) {
            fs::create_directories((agent_default / file).parent_path());
            check_outside(agent_default / file, official);
            if (file.extension() == ".info")
                write_file(agent_default / file, without_account(read_file(source / file)));
            else
                fs::copy_file(source / file, agent_default / file, fs::copy_options::overwrite_existing);
            presets += file.extension() == ".json";
        }
    return presets;
}
} // namespace

json imported_app_config(const std::string &official_text, const json &agent_config)
{
    const json official = json::parse(official_text);
    if (!official.is_object())
        throw std::runtime_error("not a JSON object");
    json result = agent_config.is_object() ? agent_config : json::object();
    for (const char *section : CONFIG_SECTIONS)
        if (official.contains(section))
            result[section] = official[section];
    for (const auto &[section, key] : CONFIG_KEYS) {
        const auto it = official.find(section);
        if (it != official.end() && it->is_object() && it->contains(key))
            result[section][key] = (*it)[key];
    }
    // The agent build never syncs presets with an account.
    result["app"]["sync_user_preset"] = false;
    return result;
}

ImportSummary import_official_data(const fs::path &official_dir, const fs::path &agent_dir, bool reimport)
{
    ImportSummary summary;
    boost::system::error_code ec;
    summary.source_found = fs::is_directory(official_dir, ec);
    if (!summary.source_found)
        return summary;

    const auto attempt = [&summary](const std::string &what, const std::function<void()> &step) {
        try {
            step();
        } catch (const std::exception &e) {
            summary.problems.push_back(what + ": " + e.what());
        }
    };
    // Nothing outside the official folder is read: a link at its top level is skipped too.
    const auto real_folder = [&ec](const fs::path &path) { return fs::is_directory(fs::symlink_status(path, ec)); };
    fs::path official;
    attempt("official folder", [&] { official = fs::canonical(official_dir); });
    if (official.empty())
        return summary;

    for (const char *folder : PROFILE_FOLDERS)
        if (real_folder(official / folder))
            attempt(folder, [&] {
                copy_tree(official / folder, agent_dir / folder, official);
                summary.folders.emplace_back(folder);
            });
    if (real_folder(official / "user"))
        attempt("user presets", [&] { summary.user_presets = copy_user_presets(official / "user", agent_dir / "user" / "default", official); });

    // Last: on first run, a missing app config means the import did not finish and runs again.
    if (fs::is_regular_file(fs::symlink_status(official / APP_CONFIG, ec)))
        attempt("preset selections", [&] {
            const fs::path agent_config_path = agent_dir / APP_CONFIG;
            check_outside(agent_config_path, official);
            const json agent_config = reimport && fs::exists(agent_config_path) ? json::parse(read_file(agent_config_path)) : json();
            write_file(agent_config_path, imported_app_config(read_file(official / APP_CONFIG), agent_config).dump(4));
            summary.config_copied = true;
        });
    return summary;
}

}}} // namespace Slic3r::GUI::AgentBridge
