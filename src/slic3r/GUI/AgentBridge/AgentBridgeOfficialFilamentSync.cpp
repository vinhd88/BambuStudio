#include "AgentBridgeOfficialFilamentSync.hpp"
#include "AgentBridgeOfficialDataFiles.hpp"

#include <map>
#include <stdexcept>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>

namespace fs = boost::filesystem;
using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
// A comma-separated list from the presets section, padded or cut to `size` with "".
std::vector<std::string> split_list(const json &presets, const char *key, size_t size)
{
    std::vector<std::string> values;
    const auto it = presets.find(key);
    if (it != presets.end() && it->is_string() && !it->get<std::string>().empty())
        boost::algorithm::split(values, it->get<std::string>(), boost::algorithm::is_any_of(","));
    values.resize(size);
    return values;
}
} // namespace

OfficialFilamentSlots read_official_filament_slots(const fs::path &official_dir)
{
    const fs::path conf_path = official_dir / "BambuStudio.conf";
    boost::system::error_code ec;
    if (!fs::is_regular_file(conf_path, ec))
        throw std::runtime_error("The official Bambu Studio's settings were not found in " + official_dir.string() + ".");
    json conf;
    try {
        conf = json::parse(read_file(conf_path));
    } catch (const std::exception &) {
        // Studio may be writing the file right now.
        throw std::runtime_error("The official Bambu Studio's settings could not be read. Close it and try again.");
    }
    const auto presets = conf.is_object() ? conf.find("presets") : conf.end();
    if (presets == conf.end() || !presets->is_object() || !presets->contains("filaments") || !(*presets)["filaments"].is_array() ||
        (*presets)["filaments"].empty())
        throw std::runtime_error("The official Bambu Studio has no filament list saved yet. Open it once, then try again.");

    OfficialFilamentSlots slots;
    for (const json &name : (*presets)["filaments"])
        slots.presets.push_back(name.is_string() ? name.get<std::string>() : std::string());
    const size_t count = slots.presets.size();
    if (presets->contains("machine") && (*presets)["machine"].is_string())
        slots.printer = (*presets)["machine"].get<std::string>();
    slots.colours       = split_list(*presets, "filament_colors", count);
    slots.multi_colours = split_list(*presets, "filament_multi_colors", count);
    for (size_t i = 0; i < count; ++i)
        if (slots.multi_colours[i].empty())
            slots.multi_colours[i] = slots.colours[i];
    for (const std::string &flag : split_list(*presets, "filament_is_mixed", count))
        slots.mixed.push_back(flag == "1");
    return slots;
}

FilamentPresetCopy copy_official_filament_presets(const fs::path &official_dir, const fs::path &agent_dir, bool dry_run)
{
    FilamentPresetCopy result;
    try {
        boost::system::error_code ec;
        const fs::path official = fs::canonical(official_dir);
        const fs::path user     = official / "user";
        if (!fs::is_directory(fs::symlink_status(user, ec)))
            return result; // no user presets in the official Studio
        // Preset (relative path without extension) -> the folder it comes from. Later folders replace
        // earlier ones, so an account wins over default; a preset's .json and .info come from one folder.
        std::map<fs::path, fs::path> sources;
        for (const fs::path &folder : user_preset_folders(user)) {
            const fs::path filaments = folder / "filament";
            if (fs::is_directory(fs::symlink_status(filaments, ec)))
                for (const fs::path &file : files_under(filaments))
                    if (file.extension() == ".json")
                        sources[fs::path(file).replace_extension()] = filaments;
        }
        std::vector<std::pair<fs::path, fs::path>> files; // relative file -> source file
        for (const auto &[preset, folder] : sources)
            for (const char *extension : {".json", ".info"}) {
                const fs::path file = fs::path(preset).concat(extension);
                if (fs::is_regular_file(fs::symlink_status(folder / file, ec)))
                    files.emplace_back(file, folder / file);
            }

        const fs::path target = agent_dir / "user" / "default" / "filament";
        for (const auto &[file, source] : files) {
            try {
                const bool        is_info = file.extension() == ".info";
                const std::string text    = is_info ? without_account(read_file(source)) : read_file(source);
                const fs::path    dest    = target / file;
                const bool        exists  = fs::is_regular_file(fs::symlink_status(dest, ec));
                const bool        same    = exists && read_file(dest) == text;
                // Checked before anything is created, and in a dry run too, so it reports what a real run would do.
                check_outside(dest, official);
                if (!same && !dry_run) {
                    fs::create_directories(dest.parent_path());
                    if (exists) {
                        if (result.backup.empty())
                            result.backup = new_backup_path(agent_dir).string();
                        const fs::path saved = fs::path(result.backup) / "user" / "default" / "filament" / file;
                        fs::create_directories(saved.parent_path());
                        fs::copy_file(dest, saved);
                    }
                    write_file(dest, text);
                }
                if (file.extension() == ".json")
                    (same ? result.unchanged : exists ? result.updated : result.added).push_back(file.stem().string());
            } catch (const std::exception &e) {
                result.problems.push_back(file.string() + ": " + e.what());
            }
        }
    } catch (const std::exception &e) {
        result.problems.push_back(std::string("filament presets: ") + e.what());
    }
    return result;
}

}}} // namespace Slic3r::GUI::AgentBridge
