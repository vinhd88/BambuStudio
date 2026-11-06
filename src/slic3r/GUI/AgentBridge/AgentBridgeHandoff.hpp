#ifndef slic3r_AgentBridgeHandoff_hpp_
#define slic3r_AgentBridgeHandoff_hpp_

#include <ctime>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI { namespace AgentBridge {

// Tool: saves a copy of the project, with this build's G-code for sliced plates, to a folder the
// bridge owns, and opens it in the official Bambu Studio, where the user prints. The open project,
// its path and its unsaved state are left alone. No caller-supplied path is ever opened. UI thread.
nlohmann::json open_in_official_studio(const nlohmann::json &args);

// GUI-free parts, for tests.

// A file name for the copy: the project name with anything unsafe replaced, then a timestamp.
std::string handoff_file_name(const std::string &project_name, std::time_t now);
// CFBundleShortVersionString from an XML Info.plist, or nullopt if it has none.
std::optional<std::string> bundle_short_version(const std::string &plist_text);
// Version strings compared number by number, so 02.09.01.70 equals 2.9.1.70.
bool same_version(const std::string &a, const std::string &b);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
