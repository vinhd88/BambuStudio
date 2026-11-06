#include "AgentBridgeDiscoveryFile.hpp"

#include <stdexcept>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#else
#include <process.h>
#endif

namespace fs = boost::filesystem;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
int current_pid()
{
#ifdef _WIN32
    return _getpid();
#else
    return static_cast<int>(getpid());
#endif
}

// Created with owner-only permissions, never widened after the fact, and renamed into place so a
// reader cannot see a partial file.
void write_owner_only_file(const std::string &path, const std::string &content)
{
    const std::string tmp_path = path + ".tmp";
#ifndef _WIN32
    ::unlink(tmp_path.c_str());
    const int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0)
        throw std::runtime_error("cannot create discovery file");
    const ssize_t written = ::write(fd, content.data(), content.size());
    ::close(fd);
    if (written != static_cast<ssize_t>(content.size())) {
        ::unlink(tmp_path.c_str());
        throw std::runtime_error("cannot write discovery file");
    }
#else
    // Inherits the ACL of the data dir, which already sits in the user's private profile folder.
    boost::nowide::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    out << content;
    out.close();
    if (!out)
        throw std::runtime_error("cannot write discovery file");
#endif
    fs::rename(tmp_path, path);
}
} // namespace

void write_discovery_file(const std::string &path, uint16_t port, const std::string &token)
{
    const nlohmann::json discovery = {{"port", port}, {"token", token}, {"pid", current_pid()}};
    write_owner_only_file(path, discovery.dump());
}

void remove_discovery_file(const std::string &path, const std::string &token)
{
    // A second instance on the same data dir may have replaced the file; leave its copy in place.
    try {
        boost::nowide::ifstream in(path);
        nlohmann::json          discovery;
        in >> discovery;
        if (discovery.value("token", std::string()) != token)
            return;
    } catch (const std::exception &) {
        return;
    }
    boost::system::error_code ignored;
    fs::remove(path, ignored);
}

}}} // namespace Slic3r::GUI::AgentBridge
