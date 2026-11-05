#include "AgentBridgeReadHandlers.hpp"

#include <functional>
#include <unordered_map>

#include <wx/app.h>
#include <wx/dialog.h>
#include <wx/evtloop.h>
#include <wx/toplevel.h>

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r_version.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/BackgroundSlicingProcess.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeProjectJson.hpp"
#include "AgentBridgeSettingsJson.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
bool modal_dialog_open()
{
    for (auto node = wxTopLevelWindows.GetFirst(); node != nullptr; node = node->GetNext())
        if (auto *dialog = dynamic_cast<wxDialog *>(node->GetData()); dialog != nullptr && dialog->IsModal())
            return true;
    return false;
}

// wxWidgets also runs queued calls while Studio is partway through a long operation: inside a nested
// event loop, or when ProgressDialog yields during an import or project load. The UI thread is then
// parked mid-operation, so Studio's state is not consistent. A progress dialog is not modal while it
// yields, so modal_dialog_open alone misses this.
bool ui_thread_is_mid_operation()
{
    const wxEventLoopBase *active = wxEventLoopBase::GetActive();
    return active != nullptr && (active->IsYielding() || active != wxTheApp->GetMainLoop());
}

json get_studio_state(const json &)
{
    Plater        &plater = *wxGetApp().plater();
    PartPlateList &plates = plater.get_partplate_list();
    PresetBundle  &bundle = *wxGetApp().preset_bundle;

    return {
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
        {"printer_preset", bundle.printers.get_edited_preset().name},
        {"process_preset", bundle.prints.get_edited_preset().name},
        {"filament_presets", bundle.filament_presets},
    };
}

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
    // Studio re-checks a plate against changed settings only when it is on screen.
    if (index != plates.get_curr_plate_index())
        out["may_be_stale"] = true;
    out.update(slice_result_to_json(*result));
    return out;
}

using Tool = std::function<json(const json &)>;

const std::unordered_map<std::string, Tool> &tools()
{
    static const std::unordered_map<std::string, Tool> table = {
        {"get_studio_state", get_studio_state}, {"get_project", get_project}, {"get_settings", get_settings},
        {"describe_settings", describe_settings}, {"list_presets", list_presets}, {"list_filaments", list_filaments},
        {"get_filament", get_filament}, {"get_slice_result", get_slice_result},
    };
    return table;
}

json parse_args(const std::string &body)
{
    if (body.empty())
        return json::object();
    json args = json::parse(body, nullptr, false);
    if (args.is_discarded() || !args.is_object())
        throw BridgeError(400, "invalid_args", "Request body must be a JSON object");
    return args;
}
} // namespace

int optional_index_arg(const json &args, const char *name, size_t count)
{
    const auto it = args.find(name);
    if (it == args.end() || it->is_null())
        return -1;
    if (!it->is_number_integer() || it->get<int64_t>() < 1)
        throw BridgeError(400, "invalid_args", std::string(name) + " must be a whole number starting at 1");
    const int64_t number = it->get<int64_t>();
    if (static_cast<uint64_t>(number) > count)
        throw BridgeError(404, "not_found", std::string(name) + " " + std::to_string(number) + " does not exist; there are " + std::to_string(count));
    return static_cast<int>(number - 1);
}

BridgeResponse handle_tool_call(const std::string &tool, const std::string &body)
{
    const auto it = tools().find(tool);
    if (it == tools().end())
        throw BridgeError(404, "unknown_tool", "Unknown tool: " + tool);

    // plater_ is not cleared at shutdown, so these checks stand between a late call and a dangling pointer.
    GUI_App &app = wxGetApp();
    if (!app.initialized() || app.is_closing() || app.is_recreating_gui() || app.mainframe == nullptr || app.plater() == nullptr || app.preset_bundle == nullptr)
        throw BridgeError(503, "studio_unavailable", "Studio is starting up or shutting down");

    if (tool != "get_studio_state") {
        if (modal_dialog_open())
            throw BridgeError(503, "studio_busy", "A dialog is open in Studio. Close it, then try again.");
        if (ui_thread_is_mid_operation())
            throw BridgeError(503, "studio_busy", "Studio is partway through an operation. Try again shortly.");
        if (app.plater()->is_loading_project())
            throw BridgeError(503, "studio_busy", "Studio is loading a project. Try again shortly.");
    }
    return ok_response(it->second(parse_args(body)));
}

}}} // namespace Slic3r::GUI::AgentBridge
