#include "AgentBridgeHandoff.hpp"

#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <wx/utils.h>

#include "libslic3r_version.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeMutationGate.hpp"
#include "AgentBridgeReadHandlers.hpp"
#include "AgentBridgeTypes.hpp"

using nlohmann::json;
namespace fs = boost::filesystem;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
const char *OFFICIAL_APP = "/Applications/BambuStudio.app";

std::vector<long> version_numbers(const std::string &version)
{
    std::vector<long> numbers;
    std::stringstream stream(version);
    for (std::string part; std::getline(stream, part, '.');)
        numbers.push_back(std::strtol(part.c_str(), nullptr, 10));
    return numbers;
}

std::optional<std::string> official_version()
{
    std::ifstream file(std::string(OFFICIAL_APP) + "/Contents/Info.plist");
    if (!file)
        return std::nullopt;
    std::stringstream text;
    text << file.rdbuf();
    return bundle_short_version(text.str());
}

// Plates with objects that have no current slice, so their copy carries no G-code.
json unsliced_plates()
{
    PartPlateList &plates = wxGetApp().plater()->get_partplate_list();
    json           list   = json::array();
    for (int i = 0; i < plates.get_plate_count(); ++i)
        if (PartPlate *plate = plates.get_plate(i); !plate->empty() && !plate->is_slice_result_valid())
            list.push_back(i + 1);
    return list;
}

// UI thread only. The outcome of the latest handoff, for get_studio_state.
json s_last_handoff;
int  s_next_handoff = 1;

json save_and_open_copy();
} // namespace

std::string handoff_file_name(const std::string &project_name, std::time_t now)
{
    std::string safe;
    for (unsigned char c : project_name)
        safe += std::isalnum(c) || c == '-' || c == '_' ? static_cast<char>(c) : '_';
    if (safe.empty())
        safe = "project";
    if (safe.size() > 60)
        safe.resize(60);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
    return safe + "-agent-" + stamp + ".3mf";
}

std::optional<std::string> bundle_short_version(const std::string &plist_text)
{
    static const std::regex pattern("<key>CFBundleShortVersionString</key>\\s*<string>([^<]*)</string>");
    std::smatch             match;
    if (!std::regex_search(plist_text, match, pattern))
        return std::nullopt;
    return match[1].str();
}

bool same_version(const std::string &a, const std::string &b) { return version_numbers(a) == version_numbers(b); }

json last_handoff_summary() { return s_last_handoff; }

json open_in_official_studio(const json &)
{
    // Saving with G-code can outlast the bridge's UI timeout. Marked running, the client is told
    // in_progress instead of timing out and retrying, which would open a second copy.
    const std::string handoff_id = "h" + std::to_string(s_next_handoff++);
    MutationScope     running("handoff_id", handoff_id);
    s_last_handoff = {{"handoff_id", handoff_id}, {"status", "running"}};
    try {
        json result          = save_and_open_copy();
        result["handoff_id"] = handoff_id;
        s_last_handoff       = result;
        return result;
    } catch (const BridgeError &e) {
        s_last_handoff = {{"handoff_id", handoff_id}, {"status", "failed"}, {"code", e.code}, {"message", e.what()}};
        throw;
    } catch (const std::exception &e) {
        s_last_handoff = {{"handoff_id", handoff_id}, {"status", "failed"}, {"code", "handoff_failed"}, {"message", e.what()}};
        throw BridgeError(500, "handoff_failed", std::string("Handoff failed: ") + e.what());
    }
}

namespace {
json save_and_open_copy()
{
    if (!fs::exists(fs::path(OFFICIAL_APP) / "Contents"))
        throw BridgeError(404, "official_app_missing", std::string("The official Bambu Studio is not installed at ") + OFFICIAL_APP + ". Nothing was saved.");

    // The copy carries this build's G-code, so the plate on screen must have a current slice.
    const json slice = get_slice_result(json::object());
    if (slice.value("state", "") != "ready" || slice.value("may_be_stale", false))
        return {{"status", "rejected"},
                {"plate", slice["plate"]},
                {"reason", "Plate " + slice["plate"].dump() + " has no current slice (" + slice.value("state", "") +
                               (slice.value("may_be_stale", false) ? ", may be stale" : "") + "). Slice it with slice_plate, wait for get_job to say done, then try again."},
                {"message", "Nothing was saved or opened."}};

    Plater &plater = *wxGetApp().plater();
    fs::path folder = fs::temp_directory_path() / "bambu-agent-handoff";
    boost::system::error_code error;
    fs::create_directories(folder, error);
    if (error)
        throw BridgeError(500, "handoff_failed", "Could not create the handoff folder: " + error.message());
    const fs::path copy = folder / handoff_file_name(plater.get_project_name().ToUTF8().data(), std::time(nullptr));

    // Silence: Studio neither adopts the copy as the project's path nor shows dialogs.
    if (plater.export_3mf(copy, SaveStrategy::Silence | SaveStrategy::WithGcode) < 0 || !fs::exists(copy))
        throw BridgeError(500, "handoff_failed", "Studio could not save the copy. The project is unchanged.");

    // No shell: the arguments go to open(1) as they are.
    const std::string app  = OFFICIAL_APP;
    const std::string file = copy.string();
    const char       *argv[] = {"/usr/bin/open", "-a", app.c_str(), file.c_str(), nullptr};
    if (::wxExecute(const_cast<char **>(argv), wxEXEC_ASYNC, nullptr) == 0)
        throw BridgeError(500, "handoff_failed", "The copy was saved to " + file + " but the official Studio could not be started.");
    BOOST_LOG_TRIVIAL(info) << "agent bridge: handed off a copy to the official Studio";

    json result = {{"status", "opened"},
                   {"copy_path", file},
                   {"plate", slice["plate"]},
                   {"this_build_version", SLIC3R_VERSION},
                   {"estimate", {{"time_s", slice.value("time_s", json())}, {"total_g", slice.value("total_g", json())}}},
                   {"message", "The copy is opening in the official Bambu Studio. The user starts the print there; agents cannot. The project here is unchanged."}};
    json warnings = json::array();
    if (const auto version = official_version()) {
        result["official_version"] = *version;
        if (!same_version(*version, SLIC3R_VERSION))
            warnings.push_back("The official Studio is version " + *version + " and this build is " + SLIC3R_VERSION + "; it may slice differently.");
    } else {
        warnings.push_back("Could not read the official Studio's version.");
    }
    if (const json plates = unsliced_plates(); !plates.empty())
        warnings.push_back("Plates " + plates.dump() + " are not sliced; their copy has no G-code.");
    if (!warnings.empty())
        result["warnings"] = std::move(warnings);
    return result;
}
} // namespace

}}} // namespace Slic3r::GUI::AgentBridge
