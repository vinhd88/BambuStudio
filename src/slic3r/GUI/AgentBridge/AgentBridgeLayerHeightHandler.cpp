#include "AgentBridgeLayerHeightHandler.hpp"

#include <algorithm>
#include <cmath>

#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Slicing.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeArgs.hpp"
#include "AgentBridgeGeometryChange.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
// What the editor uses: the project's full config and the object's height on the plate.
SlicingParameters slicing_parameters_for(const ModelObject &object)
{
    const DynamicPrintConfig config = wxGetApp().preset_bundle->full_config();
    return PrintObject::slicing_parameters(config, object, float(object.bounding_box().max.z()));
}

// Layer heights of a profile (pairs of z, height): min and max, for the result.
json profile_summary(const std::vector<double> &profile)
{
    if (profile.size() < 2)
        return {{"profile_points", 0}};
    double low = profile[1], high = profile[1];
    for (size_t i = 1; i < profile.size(); i += 2) {
        low  = std::min(low, profile[i]);
        high = std::max(high, profile[i]);
    }
    return {{"profile_points", profile.size() / 2}, {"min_layer_height_mm", std::round(low * 1000) / 1000}, {"max_layer_height_mm", std::round(high * 1000) / 1000}};
}
} // namespace

json set_variable_layer_height(const json &args)
{
    Plater    &plater = *wxGetApp().plater();
    Model     &model  = plater.model();
    const int  index  = optional_index_arg(args, "object", model.objects.size());
    if (index < 0)
        throw BridgeError(400, "invalid_args", "object is required; get_project lists them");
    const std::string mode = string_arg(args, "mode");
    if (mode != "adaptive" && mode != "smooth" && mode != "reset")
        throw BridgeError(400, "invalid_args", "mode must be adaptive, smooth or reset");
    const double quality   = optional_number_arg(args, "quality", 0, 1).value_or(0.5);
    const double radius    = optional_number_arg(args, "smooth_radius", 1, 10).value_or(5);
    const bool   keep_min  = bool_arg(args, "keep_min", false);
    // The open editor keeps its own copy of the profile and would write it back over ours.
    if (plater.get_view3D_canvas3D()->is_layers_editing_enabled())
        throw BridgeError(409, "waiting_for_user", "The variable layer height editor is open in Studio. Ask the user to close it, then try again.");
    ModelObject &object = *model.objects[index];
    if (mode == "smooth" && object.layer_height_profile.empty() && object.layer_config_ranges.empty())
        throw BridgeError(409, "nothing_to_smooth", "Object " + std::to_string(index + 1) + " has a constant layer height; use mode adaptive first.");

    json target = {{"object", index + 1}, {"mode", mode}};
    return run_geometry_change("set_variable_layer_height", target, [&](json &result) {
        if (mode == "reset") {
            object.layer_height_profile.clear();
        } else {
            const SlicingParameters params = slicing_parameters_for(object);
            std::vector<double>     profile;
            if (mode == "adaptive") {
                profile = layer_height_profile_adaptive(params, object, float(quality));
            } else {
                profile = object.layer_height_profile.empty() ? layer_height_profile_from_ranges(params, object.layer_config_ranges)
                                                              : object.layer_height_profile.get();
                profile = smooth_height_profile(profile, params, HeightProfileSmoothingParams(unsigned(radius), keep_min));
            }
            if (profile.empty())
                throw BridgeError(422, "no_profile", "Studio computed no layer height profile for this object.");
            object.layer_height_profile.set(profile);
            result.update(profile_summary(profile));
        }
        if (ObjectList *list = wxGetApp().obj_list())
            list->update_info_items(size_t(index));
        plater.schedule_background_process();
    });
}

}}} // namespace Slic3r::GUI::AgentBridge
