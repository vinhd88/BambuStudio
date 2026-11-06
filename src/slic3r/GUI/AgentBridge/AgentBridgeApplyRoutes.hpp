#ifndef slic3r_AgentBridgeApplyRoutes_hpp_
#define slic3r_AgentBridgeApplyRoutes_hpp_

#include <memory>
#include <string>
#include <vector>

#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Preset.hpp"

namespace Slic3r { namespace GUI {
class PartPlate;
namespace AgentBridge {

struct SettingChange
{
    std::string                   key;
    std::shared_ptr<ConfigOption> value;
};

// How each scope reaches Studio, so a change has the same effect as making it in the UI: dirty marks,
// sidebar sync, linked settings and any dialog Studio would show. UI thread only. Can block inside a
// dialog; the caller's client has been answered by then.

// Print or filament tab, whichever `tab_type` names, editing the selected preset.
void apply_to_preset_tab(Preset::Type tab_type, const std::vector<SettingChange> &changes);
void apply_to_plate(PartPlate &plate, const std::vector<SettingChange> &changes);
void apply_to_object(ModelObject &object, const std::vector<SettingChange> &changes);
void apply_to_part(ModelObject &object, ModelVolume &part, const std::vector<SettingChange> &changes);
void apply_to_height_range(ModelObject &object, const t_layer_height_range &range, const std::vector<SettingChange> &changes);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
