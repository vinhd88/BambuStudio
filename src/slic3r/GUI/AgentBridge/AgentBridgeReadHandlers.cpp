#include "AgentBridgeReadHandlers.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r_version.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/BackgroundSlicingProcess.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeApplyHandlers.hpp"
#include "AgentBridgeArgs.hpp"
#include "AgentBridgeModalGuard.hpp"
#include "AgentBridgeMutationGate.hpp"
#include "AgentBridgeProjectJson.hpp"
#include "AgentBridgeSettingsJson.hpp"
#include "AgentBridgeSliceJobs.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
json plate_to_json(PartPlate &plate, int current_index)
{
    json instances = json::array();
    for (const auto &instance : plate.get_obj_and_inst_set())
        instances.push_back({{"object", instance.first + 1}, {"instance", instance.second + 1}});

    json overrides = json::array();
    for (const std::string &key : plate.config()->keys())
        if (is_user_setting(key))
            overrides.push_back(key);

    json out = {{"plate", plate.get_index() + 1}, {"name", plate.get_plate_name()}, {"instances", std::move(instances)}, {"sliced", plate.is_slice_result_valid()}};
    if (plate.get_index() == current_index)
        out["current"] = true;
    if (!overrides.empty())
        out["overrides"] = std::move(overrides);
    return out;
}
} // namespace

json get_studio_state(const json &)
{
    Plater        &plater = *wxGetApp().plater();
    PartPlateList &plates = plater.get_partplate_list();
    PresetBundle  &bundle = *wxGetApp().preset_bundle;

    json state = {
        {"studio_version", SLIC3R_VERSION},
        {"project_name", plater.get_project_name().ToUTF8().data()},
        {"project_path", plater.get_project_filename(".3mf").ToUTF8().data()},
        {"unsaved_changes", plater.is_project_dirty()},
        {"loading_project", plater.is_loading_project()},
        {"object_count", plater.model().objects.size()},
        {"plate_count", plates.get_plate_count()},
        {"current_plate", plates.get_curr_plate_index() + 1},
        {"slicing", plater.is_background_process_slicing()},
        {"dialog_open", modal_dialog_open()},
        {"dialog_title", open_dialog_title()},
        {"printer_preset", bundle.printers.get_edited_preset().name},
        {"process_preset", bundle.prints.get_edited_preset().name},
        {"filament_presets", bundle.filament_presets},
    };
    if (const std::string running = running_mutation_id(); !running.empty())
        state["running"] = running;
    if (json last = last_change_summary(); !last.is_null())
        state["last_change"] = std::move(last);
    return state;
}

json get_project(const json &)
{
    Plater        &plater = *wxGetApp().plater();
    PartPlateList &plates = plater.get_partplate_list();
    PresetBundle  &bundle = *wxGetApp().preset_bundle;

    json plate_list = json::array();
    for (int i = 0; i < plates.get_plate_count(); ++i)
        plate_list.push_back(plate_to_json(*plates.get_plate(i), plates.get_curr_plate_index()));

    const auto *colours = bundle.project_config.option<ConfigOptionStrings>("filament_colour");
    json        slots   = json::array();
    for (size_t i = 0; i < bundle.filament_presets.size(); ++i) {
        json slot = {{"slot", i + 1}, {"preset", bundle.filament_presets[i]}};
        if (colours != nullptr && i < colours->values.size())
            slot["colour"] = colours->values[i];
        if (const Preset *preset = bundle.filaments.find_preset(bundle.filament_presets[i], false))
            slot["type"] = first_string_value(preset->config, "filament_type");
        slots.push_back(std::move(slot));
    }

    return {
        {"name", plater.get_project_name().ToUTF8().data()},
        {"plates", std::move(plate_list)},
        {"objects", model_objects_to_json(plater.model())},
        {"filaments", std::move(slots)},
    };
}

json get_slice_result(const json &args)
{
    Plater        &plater = *wxGetApp().plater();
    PartPlateList &plates = plater.get_partplate_list();

    int index = optional_index_arg(args, "plate", plates.get_plate_count());
    if (index < 0)
        index = plates.get_curr_plate_index();
    PartPlate &plate = *plates.get_plate(index);

    json out = {{"plate", index + 1}};
    // The plate being sliced, which can differ from the one on screen. Its result is being rewritten.
    if (plater.is_background_process_slicing() && plater.background_process().get_current_plate() == &plate) {
        out["state"]            = "slicing";
        out["progress_percent"] = plate.get_slicing_percent();
        return out;
    }
    const GCodeProcessorResult *result = plate.get_slice_result();
    if (!plate.is_slice_result_valid() || result == nullptr) {
        out["state"] = plate.empty() ? "empty_plate" : "not_sliced";
        return out;
    }
    out["state"] = "ready";
    // Studio re-checks a plate against changed settings only when it is on screen, and only some time
    // after the change, so a result read right after apply_settings can still look current.
    if (index != plates.get_curr_plate_index() || settings_changed_since_last_slice())
        out["may_be_stale"] = true;
    out.update(slice_result_to_json(*result));
    return out;
}

}}} // namespace Slic3r::GUI::AgentBridge
