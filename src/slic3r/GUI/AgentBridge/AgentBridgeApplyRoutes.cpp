#include "AgentBridgeApplyRoutes.hpp"

#include <map>
#include <stdexcept>

#include <boost/any.hpp>

#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/ObjectDataViewModel.hpp"
#include "slic3r/GUI/ParamsPanel.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Tab.hpp"

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
// The value in the form a UI field passes to Tab::on_value_change. Handlers there cast it to the
// exact type a field of that kind sends, and bad_any_cast otherwise.
boost::any value_for_handlers(const ConfigOption &option)
{
    switch (option.type()) {
    case coBool:             return option.getBool();
    case coBools:            return static_cast<unsigned char>(static_cast<const ConfigOptionBools &>(option).values.front());
    case coInt:              return option.getInt();
    case coEnum:             return option.getInt();
    case coInts:             return static_cast<const ConfigOptionInts &>(option).values.front();
    case coFloat:            return option.getFloat();
    case coPercent:          return option.getFloat();
    case coFloats:           return static_cast<const ConfigOptionFloats &>(option).values.front();
    case coPercents:         return static_cast<const ConfigOptionFloats &>(option).values.front();
    case coString:           return static_cast<const ConfigOptionString &>(option).value;
    default:                 return option.serialize();
    }
}

// Sets the values, then runs the tab's change handler key by key, as editing the fields would. The
// handler can change or roll back values, so what it leaves is read back from the config afterwards.
void set_values_and_notify(Tab &tab, const std::vector<SettingChange> &changes)
{
    DynamicPrintConfig &config = *tab.get_config();
    for (const SettingChange &change : changes)
        config.set_key_value(change.key, change.value->clone());
    tab.update_dirty();
    try {
        for (const SettingChange &change : changes) {
            // An empty list has no element to pass; nothing in the handlers looks at those.
            const auto *vec = dynamic_cast<const ConfigOptionVectorBase *>(change.value.get());
            tab.on_value_change(change.key, vec != nullptr && vec->size() == 0 ? boost::any() : value_for_handlers(*change.value));
        }
    } catch (...) {
        // The fields must still show what the config now holds.
        tab.reload_config();
        throw;
    }
    tab.reload_config();
}

// The model tabs edit what is selected in the object list. Pointing one at the target for the length
// of a change reuses its handlers, and the user's selection is left alone. A modal dialog raised
// inside a change blocks the object list, so the selection cannot move in the meantime.
class ModelTabTarget
{
public:
    ModelTabTarget(Tab *tab, ObjectBase *target, ModelConfig *config)
        : m_tab(checked(tab)), m_previous(m_tab->get_object_configs())
    {
        m_tab->set_model_config({{target, config}});
    }
    ~ModelTabTarget() { m_tab->set_model_config(m_previous); }

    ModelTabTarget(const ModelTabTarget &) = delete;
    ModelTabTarget &operator=(const ModelTabTarget &) = delete;

    Tab &tab() { return *m_tab; }

private:
    static TabPrintModel *checked(Tab *tab)
    {
        auto *model_tab = dynamic_cast<TabPrintModel *>(tab);
        if (model_tab == nullptr)
            throw std::runtime_error("Studio's settings tab for this level is not available");
        return model_tab;
    }

    TabPrintModel                         *m_tab;
    std::map<ObjectBase *, ModelConfig *>  m_previous;
};

} // namespace

void refresh_after_model_change()
{
    wxGetApp().mainframe->on_config_changed(wxGetApp().get_tab(Preset::TYPE_PRINT)->get_config());
    wxGetApp().params_panel()->notify_object_config_changed();
    wxGetApp().plater()->update();
}

void apply_to_preset_tab(Preset::Type tab_type, const std::vector<SettingChange> &changes)
{
    set_values_and_notify(*wxGetApp().get_tab(tab_type), changes);
}

void apply_to_plate(PartPlate &plate, const std::vector<SettingChange> &changes)
{
    // As ObjectSettings does: the tab works on a copy, and writes through to the plate by its setters.
    ModelConfig copy;
    copy.assign_config(*plate.config());
    {
        ModelTabTarget target(wxGetApp().get_plate_tab(), &plate, &copy);
        set_values_and_notify(target.tab(), changes);
    }
    refresh_after_model_change();
}

void apply_to_object(ModelObject &object, const std::vector<SettingChange> &changes)
{
    {
        ModelTabTarget target(wxGetApp().get_model_tab(), &object, &object.config);
        set_values_and_notify(target.tab(), changes);
    }
    refresh_after_model_change();
}

void apply_to_part(ModelObject &object, ModelVolume &part, const std::vector<SettingChange> &changes)
{
    {
        // The part tab inherits its unset values from the object tab.
        ModelTabTarget parent(wxGetApp().get_model_tab(), &object, &object.config);
        ModelTabTarget target(wxGetApp().get_model_tab(true), &part, &part.config);
        set_values_and_notify(target.tab(), changes);
    }
    refresh_after_model_change();
}

void apply_to_height_range(ModelObject &object, const t_layer_height_range &range, const std::vector<SettingChange> &changes)
{
    // Not through TabPrintLayer: its change handler adds the settings item to whatever is selected in
    // the object list, which here need not be this range. The layer tab has no dialogs to lose.
    ModelConfig &config = object.layer_config_ranges.at(range);
    for (const SettingChange &change : changes)
        config.set_key_value(change.key, change.value->clone());
    // A range always carries its own layer height; TabPrintLayer adds it the same way.
    if (!config.has("layer_height"))
        config.set_key_value("layer_height", wxGetApp().preset_bundle->prints.get_edited_preset().config.option("layer_height")->clone());
    config.touch();

    if (Tab *tab = wxGetApp().get_layer_tab(); tab != nullptr)
        if (auto *layer_tab = dynamic_cast<TabPrintModel *>(tab); layer_tab != nullptr && layer_tab->has_model_config())
            layer_tab->set_model_config(layer_tab->get_object_configs());
    refresh_after_model_change();
}

}}} // namespace Slic3r::GUI::AgentBridge
