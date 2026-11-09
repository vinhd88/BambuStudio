#include "AgentBridgeLayoutHandlers.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/filesystem.hpp>

#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Jobs/Job.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeArgs.hpp"
#include "AgentBridgeCanvasSelection.hpp"
#include "AgentBridgeGeometryChange.hpp"
#include "AgentBridgeGeometryJobs.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
Plater &plater() { return *wxGetApp().plater(); }

void require_objects()
{
    if (plater().model().objects.empty())
        throw BridgeError(409, "empty_project", "The project has no objects.");
}

std::vector<int> all_objects()
{
    std::vector<int> all(plater().model().objects.size());
    for (size_t i = 0; i < all.size(); ++i)
        all[i] = int(i);
    return all;
}

// Studio's job reads its prepare state when it starts, so it is set just before and put back after.
void start_job(int prepare_state, const std::function<void()> &start)
{
    plater().set_prepare_state(prepare_state);
    try {
        start();
    } catch (...) {
        plater().set_prepare_state(Job::PREPARE_STATE_DEFAULT);
        throw;
    }
    plater().set_prepare_state(Job::PREPARE_STATE_DEFAULT);
}

void note_job(json &result, const std::string &kind)
{
    result["job_id"] = track_geometry_job(result["change_id"].get<std::string>(), kind);
    result["status"] = "running";
    result["message"] = "Studio is working on it; call get_job with job_id until state is done.";
}

} // namespace

json orient_objects(const json &args)
{
    require_objects();
    const std::vector<int> objects = object_list_arg(args, "objects");
    const int              plate   = optional_index_arg(args, "plate", plater().get_partplate_list().get_plate_count());
    if (!objects.empty() && plate >= 0)
        throw BridgeError(400, "invalid_args", "Give objects or plate, not both");
    json target = json::object();
    if (plate >= 0)
        target["plate"] = plate + 1;
    if (!objects.empty())
        target["objects"] = args["objects"];

    return run_geometry_change("orient_objects", target, [&](json &result) {
        if (plate >= 0) {
            plater().select_plate(plate);
            start_job(Job::PREPARE_STATE_MENU, [] { plater().orient(); });
        } else {
            select_objects(objects.empty() ? all_objects() : objects);
            start_job(Job::PREPARE_STATE_DEFAULT, [] { plater().orient(); });
        }
        note_job(result, "orient");
    });
}

json arrange_plate(const json &args)
{
    require_objects();
    const int  plate          = optional_index_arg(args, "plate", plater().get_partplate_list().get_plate_count());
    const auto spacing        = optional_number_arg(args, "spacing_mm", 0, 100);
    const auto allow_rotation = optional_bool_arg(args, "allow_rotation");
    const auto align_y        = optional_bool_arg(args, "align_to_y_axis");
    json       target         = {{"plate", plate >= 0 ? json(plate + 1) : json("all")}};

    return run_geometry_change("arrange_plate", target, [&](json &result) {
        // The user's own arrange options stay as they were: overrides apply to this run only.
        GLCanvas3D::ArrangeSettings &settings = plater().canvas3D()->get_arrange_settings();
        const auto                   saved    = settings;
        if (spacing)        settings.distance        = float(*spacing);
        if (allow_rotation) settings.enable_rotation = *allow_rotation;
        if (align_y)        settings.align_to_y_axis = *align_y;
        try {
            if (plate >= 0) {
                plater().select_plate(plate);
                start_job(Job::PREPARE_STATE_MENU, [] { plater().arrange(); });
            } else {
                start_job(Job::PREPARE_STATE_DEFAULT, [] { plater().arrange(); });
            }
        } catch (...) {
            settings = saved;
            throw;
        }
        settings = saved;
        note_job(result, "arrange");
    });
}

json add_plate(const json &)
{
    if (!plater().can_add_plate())
        throw BridgeError(409, "plate_limit", "Studio cannot add another plate (it allows a limited number).");
    return run_geometry_change("add_plate", json::object(), [](json &result) {
        plater().get_partplate_list().add_plate();
        plater().update();
        result["plate"] = plater().get_partplate_list().get_plate_count();
    });
}

json add_model(const json &args)
{
    namespace fs = boost::filesystem;
    const std::string path = string_arg(args, "path");
    if (path.empty() || !fs::path(path).is_absolute())
        throw BridgeError(400, "invalid_args", "path must be an absolute path to a model file");
    boost::system::error_code ec;
    if (!fs::is_regular_file(path, ec))
        throw BridgeError(404, "not_found", "No file at " + path);
    const std::string ext = boost::algorithm::to_lower_copy(fs::path(path).extension().string());
    static const std::set<std::string> allowed = {".stl", ".3mf", ".obj", ".step", ".stp", ".amf"};
    if (allowed.count(ext) == 0)
        throw BridgeError(400, "invalid_args", "Only .stl, .3mf, .obj, .step, .stp and .amf files can be added");

    return run_geometry_change("add_model", {{"path", path}}, [&](json &result) {
        // Model only: a 3MF's printer and process settings never replace the project's.
        const std::vector<size_t> added = plater().load_files(std::vector<std::string>{path}, LoadStrategy::LoadModel, false);
        if (added.empty())
            throw BridgeError(422, "load_failed", "Studio could not load any object from " + path + " (see Studio's notifications).");
        json numbers = json::array();
        for (size_t index : added)
            numbers.push_back(index + 1);
        result["added_objects"] = std::move(numbers);
    });
}

json split_object(const json &args)
{
    Model     &model = plater().model();
    const int  index = optional_index_arg(args, "object", model.objects.size());
    if (index < 0)
        throw BridgeError(400, "invalid_args", "object is required; get_project lists them");
    const std::string into = string_arg(args, "into", "objects");
    if (into != "objects" && into != "parts")
        throw BridgeError(400, "invalid_args", "into must be objects or parts");
    const bool to_objects = into == "objects";
    // Studio enables the split buttons for any single object and only says afterwards when there was
    // nothing to split, so check for separate pieces first.
    size_t pieces = 0;
    for (const ModelVolume *volume : model.objects[index]->volumes)
        if (volume->is_model_part())
            pieces += its_number_of_patches(volume->mesh().its);
    if (pieces < 2)
        throw BridgeError(409, "cannot_split", "Object " + std::to_string(index + 1) + " is one connected piece, so there is nothing to split.");
    select_objects({index});
    if (!plater().can_split(to_objects))
        throw BridgeError(409, "cannot_split", "Object " + std::to_string(index + 1) + " cannot be split into " + into +
                                                   ": it is one connected piece, or Studio does not allow it here.");

    const size_t objects_before = model.objects.size();
    const size_t parts_before   = model.objects[index]->volumes.size();
    return run_geometry_change("split_object", {{"object", index + 1}, {"into", into}}, [&](json &result) {
        if (to_objects)
            plater().split_object(model.objects[index], true);
        else
            plater().split_volume();
        // Studio merges tiny or nested shells, so it can find nothing to split after all.
        if (model.objects.size() == objects_before && model.objects[index]->volumes.size() == parts_before) {
            result["status"]  = "unchanged";
            result["message"] = "Studio found nothing to split: the separate shells are too small or inside each other.";
        }
    });
}

}}} // namespace Slic3r::GUI::AgentBridge
