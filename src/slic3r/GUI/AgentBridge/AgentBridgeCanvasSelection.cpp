#include "AgentBridgeCanvasSelection.hpp"

#include <set>

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Selection.hpp"

#include "AgentBridgeArgs.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
Selection &selection() { return wxGetApp().plater()->get_view3D_canvas3D()->get_selection(); }

// Keeps the object list on the left in step with the 3D view, as a click would.
void sync_object_list()
{
    if (ObjectList *list = wxGetApp().obj_list())
        list->update_selections();
    wxGetApp().plater()->get_view3D_canvas3D()->set_as_dirty();
}
} // namespace

std::vector<int> object_list_arg(const json &args, const char *name)
{
    const auto it = args.find(name);
    if (it == args.end() || it->is_null())
        return {};
    if (!it->is_array())
        throw BridgeError(400, "invalid_args", std::string(name) + " must be an array of object numbers");
    const size_t  count = wxGetApp().plater()->model().objects.size();
    std::set<int> seen;
    std::vector<int> out;
    for (const json &entry : *it) {
        const int index = optional_index_arg(json{{name, entry}}, name, count);
        if (index >= 0 && seen.insert(index).second)
            out.push_back(index);
    }
    return out;
}

void select_objects(const std::vector<int> &object_indexes)
{
    Selection &sel = selection();
    sel.clear();
    for (int index : object_indexes)
        sel.add_object(static_cast<unsigned int>(index), false);
    sync_object_list();
}

void select_instance(int object_index, int instance_index)
{
    Selection &sel = selection();
    sel.clear();
    sel.add_instance(static_cast<unsigned int>(object_index), static_cast<unsigned int>(instance_index), true);
    sync_object_list();
}

}}} // namespace Slic3r::GUI::AgentBridge
