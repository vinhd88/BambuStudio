#ifndef slic3r_AgentBridgeOfficialDataFiles_hpp_
#define slic3r_AgentBridgeOfficialDataFiles_hpp_

#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>

// File helpers shared by everything that copies from the official Studio's data folder into the
// agent build's folder: the full import and the filament sync. GUI-free.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// The whole file, bytes as they are. Throws if it cannot be read.
std::string read_file(const boost::filesystem::path &path);

// Written beside the target and renamed into place, so a crash never leaves half a file. Throws.
void write_file(const boost::filesystem::path &path, const std::string &text);

// Refuses a write that would land in the official folder, for example through a link the user made
// from the agent folder into it. Resolves links in the target's folder; the folder must exist.
void check_outside(const boost::filesystem::path &target, const boost::filesystem::path &official);

// Regular files under `from`, as sorted paths relative to it. Links inside are skipped and not
// entered; callers skip `from` itself if it is a link.
std::vector<boost::filesystem::path> files_under(const boost::filesystem::path &from);

// A preset's .info file without the account it was synced with: the agent build is logged out.
std::string without_account(const std::string &info);

// The folders under the official "user" folder (one per account, plus "default"), in copy order:
// "default" first, so an account's preset wins a name clash. Links to folders are skipped.
std::vector<boost::filesystem::path> user_preset_folders(const boost::filesystem::path &official_user);

// A "<agent>-backup-<time>" path beside the agent folder that nothing has used yet.
boost::filesystem::path new_backup_path(const boost::filesystem::path &agent_dir);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
