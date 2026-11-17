#include "AgentBridgeOfficialDataFiles.hpp"

#include <algorithm>
#include <ctime>
#include <iterator>
#include <sstream>
#include <stdexcept>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

namespace fs = boost::filesystem;

namespace Slic3r { namespace GUI { namespace AgentBridge {

std::string read_file(const fs::path &path)
{
    boost::nowide::ifstream in(path.string(), std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + path.filename().string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_file(const fs::path &path, const std::string &text)
{
    const fs::path temp = path.string() + ".importing";
    // A leftover, or a link someone placed there, must not be written through.
    boost::system::error_code ec;
    fs::remove(temp, ec);
    {
        boost::nowide::ofstream out(temp.string(), std::ios::binary | std::ios::trunc);
        out << text;
        if (!out.flush())
            throw std::runtime_error("cannot write " + path.filename().string());
    }
    fs::rename(temp, path);
}

void check_outside(const fs::path &target, const fs::path &official)
{
    const fs::path folder = fs::weakly_canonical(target.parent_path());
    const fs::path inside = folder.lexically_relative(official);
    if (!inside.empty() && *inside.begin() != "..")
        throw std::runtime_error("refused to write into the official Studio folder: " + target.filename().string());
}

std::vector<fs::path> files_under(const fs::path &from)
{
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(from), end; it != end; ++it)
        if (fs::is_regular_file(it->symlink_status()))
            files.push_back(it->path().lexically_relative(from));
    std::sort(files.begin(), files.end());
    return files;
}

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

std::vector<fs::path> user_preset_folders(const fs::path &official_user)
{
    std::vector<fs::path> folders;
    for (const fs::directory_entry &entry : fs::directory_iterator(official_user))
        if (fs::is_directory(entry.symlink_status())) // a link to a folder is skipped
            folders.push_back(entry.path());
    std::sort(folders.begin(), folders.end(), [](const fs::path &a, const fs::path &b) {
        return (a.filename() == "default") != (b.filename() == "default") ? a.filename() == "default" : a < b;
    });
    return folders;
}

fs::path new_backup_path(const fs::path &agent_dir)
{
    const std::time_t now = std::time(nullptr);
    std::tm           local{};
    localtime_r(&now, &local);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    const std::string base = agent_dir.lexically_normal().string() + "-backup-" + stamp;
    fs::path          path = base;
    for (int n = 2; fs::exists(path); ++n)
        path = base + "-" + std::to_string(n);
    return path;
}

}}} // namespace Slic3r::GUI::AgentBridge
