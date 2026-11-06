#include "AgentBridgeTargetRoutes.hpp"

#include <stdexcept>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
// The project-wide bed type, through the sidebar combo, as picking it there does: Studio writes the
// project config, marks the project dirty and re-checks every plate. Validation has made sure the
// combo offers the type; otherwise Studio would quietly pick another one.
void apply_project_bed_type(const std::vector<SettingChange> &changes)
{
    for (const SettingChange &change : changes) {
        if (change.key != "curr_bed_type")
            throw std::runtime_error("only the bed type can be set project-wide");
        wxGetApp().plater()->sidebar().set_bed_type_accord_combox(static_cast<BedType>(change.value->getInt()));
    }
}

void remove_from_plate(PartPlate &plate, const std::vector<std::string> &keys)
{
    // The same setters TabPrintPlate's reset uses. Each one erases the key.
    for (const std::string &key : keys) {
        if (key == "curr_bed_type")
            plate.reset_bed_type();
        else if (key == "print_sequence")
            plate.set_print_seq(PrintSequence::ByDefault);
        else if (key == "spiral_mode")
            plate.set_spiral_vase_mode(false, true);
        else
            plate.config()->erase(key);
    }
    plate.update_slice_result_valid_state(false);
}
} // namespace

void apply_to_target(const Target &target, Preset::Type tab_type, const std::vector<SettingChange> &changes)
{
    switch (target.scope) {
    case ApplyScope::Plate:       apply_to_plate(*target.plate, changes); break;
    case ApplyScope::Object:      apply_to_object(*target.object, changes); break;
    case ApplyScope::Part:        apply_to_part(*target.object, *target.part, changes); break;
    case ApplyScope::HeightRange: apply_to_height_range(*target.object, target.range, changes); break;
    default:
        if (tab_type == PROJECT_TAB)
            apply_project_bed_type(changes);
        else
            apply_to_preset_tab(tab_type, changes);
        break;
    }
}

void remove_from_target(const Target &target, const std::vector<std::string> &keys)
{
    if (keys.empty())
        return;
    switch (target.scope) {
    case ApplyScope::Plate:
        remove_from_plate(*target.plate, keys);
        break;
    case ApplyScope::Object:
        for (const std::string &key : keys)
            target.object->config.erase(key);
        // Rebuilds the object's settings item in the object list, as the tab's reset does.
        wxGetApp().obj_list()->object_config_options_changed({target.object, nullptr});
        break;
    case ApplyScope::Part:
        for (const std::string &key : keys)
            target.part->config.erase(key);
        wxGetApp().obj_list()->object_config_options_changed({target.object, target.part});
        break;
    case ApplyScope::HeightRange: {
        ModelConfig &config = target.object->layer_config_ranges.at(target.range);
        for (const std::string &key : keys)
            // A range keeps its layer height: TabPrintLayer adds it back whenever it is missing.
            if (key != "layer_height")
                config.erase(key);
        break;
    }
    default:
        throw std::runtime_error("settings of a preset cannot be removed, only changed");
    }
    refresh_after_model_change();
}

}}} // namespace Slic3r::GUI::AgentBridge
