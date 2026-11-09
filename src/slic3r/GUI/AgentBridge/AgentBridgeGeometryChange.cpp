#include "AgentBridgeGeometryChange.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include <boost/log/trivial.hpp>

#include "libslic3r/Model.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosManager.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include "AgentBridgeApplyHandlers.hpp"
#include "AgentBridgeGeometryRevertCheck.hpp"
#include "AgentBridgeMutationGate.hpp"
#include "AgentBridgeTypes.hpp"

using nlohmann::json;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
Plater &plater() { return *wxGetApp().plater(); }

bool assembly_view_shown()
{
    GLCanvas3D *canvas = plater().get_current_canvas3D();
    return canvas != nullptr && canvas->get_canvas_type() == GLCanvas3D::CanvasAssembleView;
}

std::optional<size_t> newest_main_snapshot() { return newest_snapshot_time(plater().undo_redo_stack_main().snapshots()); }

json round_mm(double value) { return std::round(value * 100.0) / 100.0; }

json refused(const std::string &reason)
{
    return {{"status", "refused"}, {"reason", reason}, {"message", "Nothing was reverted."}};
}
} // namespace

void prepare_for_geometry_change()
{
    if (assembly_view_shown())
        throw BridgeError(409, "wrong_view", "Studio shows the assembly view. Ask the user to switch back to the Prepare view, then try again.");
    GLCanvas3D *canvas = plater().get_view3D_canvas3D();
    if (canvas == nullptr)
        throw BridgeError(503, "studio_busy", "The 3D view is not ready. Try again shortly.");
    GLGizmosManager &gizmos = canvas->get_gizmos_manager();
    // An open gizmo works on its own undo stack; closing it keeps what the user did in it.
    gizmos.reset_all_states();
    if (gizmos.get_current_type() != GLGizmosManager::Undefined)
        throw BridgeError(409, "waiting_for_user", "A tool is open in Studio's 3D view and would not close. Ask the user to close it, then try again.");
}

json objects_summary()
{
    const Model &model   = plater().model();
    json         objects = json::array();
    for (size_t i = 0; i < model.objects.size(); ++i) {
        const ModelObject &object = *model.objects[i];
        json               entry  = {{"object", i + 1}, {"name", object.name}, {"instances", object.instances.size()}, {"parts", object.volumes.size()}};
        if (!object.instances.empty()) {
            const BoundingBoxf3 box = object.instance_bounding_box(0);
            entry["size_mm"]        = {round_mm(box.size().x()), round_mm(box.size().y()), round_mm(box.size().z())};
        }
        objects.push_back(std::move(entry));
    }
    return objects;
}

json run_geometry_change(const std::string &tool, json target, const std::function<void(json &result)> &work)
{
    prepare_for_geometry_change();
    ChangeJournal    &journal   = change_journal();
    const std::string change_id = journal.begin(target, {}, ChangeKind::Geometry);
    MutationScope     running("change_id", change_id);
    const std::string snapshot_name = std::string(AGENT_SNAPSHOT_PREFIX) + tool + " " + change_id;
    // Closes the record when the project was certainly not changed, so it never blocks older reverts.
    const auto nothing_changed = [&](const json &outcome) {
        journal.mark_reverted(change_id);
        journal.finish(change_id, outcome);
    };

    json result         = target.is_object() ? target : json::object();
    result["change_id"] = change_id;
    std::optional<size_t> first;
    const auto record_range = [&]() {
        if (ChangeRecord *record = journal.find(change_id); record != nullptr && first)
            record->restore = {{"first_snapshot", *first}, {"last_snapshot", newest_main_snapshot().value_or(*first)}, {"first_name", snapshot_name}};
    };
    try {
        // Snapshots taken inside (e.g. Studio's own "Arrange") are suppressed: the change is one undo step.
        Plater::TakeSnapshot snapshot(&plater(), snapshot_name);
        const auto           newest = newest_main_snapshot();
        const auto          &stack  = plater().undo_redo_stack_main().snapshots();
        const auto           ours   = std::find_if(stack.begin(), stack.end(), [&](const UndoRedo::Snapshot &s) { return newest && s.timestamp == *newest; });
        if (ours == stack.end() || ours->name != snapshot_name) {
            // Without our snapshot nothing could undo the change, so it must not happen.
            nothing_changed({{"status", "failed"}, {"reason", "Studio did not record an undo step"}});
            throw BridgeError(503, "studio_busy", "Studio did not record an undo step, so the change was not made. Try again shortly.");
        }
        first = newest;
        work(result);
    } catch (const BridgeError &e) {
        // Tools throw 4xx only before touching the project; 5xx may come after part of it landed.
        if (ChangeRecord *record = journal.find(change_id); record != nullptr && !record->reverted) {
            const json outcome = {{"status", "failed"}, {"code", e.code}, {"reason", e.what()}};
            if (e.status < 500 || !first) {
                nothing_changed(outcome);
            } else {
                record_range();
                journal.finish(change_id, outcome);
            }
        }
        throw;
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "agent bridge: " << change_id << " failed: " << e.what();
        if (!first) {
            nothing_changed({{"status", "failed"}, {"reason", e.what()}});
            throw BridgeError(500, "change_failed", "Change " + change_id + " failed before it started: " + e.what() + ". Nothing was changed.");
        }
        record_range();
        journal.finish(change_id, {{"status", "failed"}, {"reason", e.what()}});
        throw BridgeError(500, "change_failed", "Change " + change_id + " failed partway: " + e.what() + ". revert_change can undo what landed.");
    }
    record_range();

    if (!result.contains("status"))
        result["status"] = "applied";
    result["objects"] = objects_summary();
    // Studio declined to do anything (e.g. nothing to split): nothing to revert.
    if (result["status"] == "unchanged")
        nothing_changed(result);
    else
        journal.finish(change_id, result);
    return result;
}

namespace {
// Redo targets noted when agent geometry changes were reverted; see geometry_revert_rejection.
std::vector<RedoMarker> redo_markers()
{
    std::vector<RedoMarker> markers;
    for (const ChangeRecord &r : change_journal().records())
        if (r.kind == ChangeKind::Geometry && r.restore.contains("redo_time"))
            markers.push_back({r.restore["redo_time"].get<size_t>(), r.restore.value("redo_name", "")});
    return markers;
}

std::optional<std::string> rejection_now(const GeometrySnapshotRange &range)
{
    const UndoRedo::Stack &stack = plater().undo_redo_stack_main();
    return geometry_revert_rejection(stack.snapshots(), stack.active_snapshot_time(), range, redo_markers());
}
} // namespace

json revert_geometry_change(const ChangeRecord &record)
{
    if (assembly_view_shown())
        return refused("Studio shows the assembly view, which has its own undo history. Ask the user to switch back to the Prepare view.");
    if (plater().is_any_job_running())
        return refused("A Studio job (arrange, orient, ...) is still running. Try again when it finishes.");
    if (!record.restore.contains("first_snapshot"))
        return refused("This change has no undo step in Studio.");

    // A later change that was skipped is still in the project; jumping back would silently undo it too.
    const auto &records = change_journal().records();
    const auto  self    = std::find_if(records.begin(), records.end(), [&](const ChangeRecord &r) { return r.change_id == record.change_id; });
    for (auto it = self == records.end() ? records.end() : std::next(self); it != records.end(); ++it)
        if (it->outcome.contains("revert") && it->outcome["revert"].value("status", "") == "skipped")
            return refused("Later change " + it->change_id + " was skipped and is still in the project; undoing this one would undo it too.");

    GeometrySnapshotRange range;
    range.first_time = record.restore["first_snapshot"].get<size_t>();
    range.last_time  = record.restore.value("last_snapshot", range.first_time);
    range.first_name = record.restore.value("first_name", "");

    // Checked before closing any open tool, so a refusal leaves Studio as it is; again after, because
    // closing a tool can record the user's last edit in it.
    if (const auto reason = rejection_now(range))
        return refused(*reason);
    prepare_for_geometry_change();
    if (const auto reason = rejection_now(range))
        return refused(*reason);

    plater().undo_redo_to(range.first_time);
    const UndoRedo::Stack &stack = plater().undo_redo_stack_main();
    if (stack.active_snapshot_time() != range.first_time)
        throw BridgeError(500, "revert_failed", "Studio's undo did not land before change " + record.change_id + "; check the project. revert_change with skip moves past it.");
    // What a Redo would load next; if the user redoes, older reverts must not undo it silently.
    const auto &snapshots = stack.snapshots();
    const auto  next      = std::upper_bound(snapshots.begin(), snapshots.end(), UndoRedo::Snapshot(range.first_time));
    if (ChangeRecord *mutable_record = change_journal().find(record.change_id); mutable_record != nullptr && next != snapshots.end()) {
        mutable_record->restore["redo_time"] = next->timestamp;
        mutable_record->restore["redo_name"] = next->name;
    }
    return {{"status", "reverted"}, {"objects", objects_summary()}};
}

}}} // namespace Slic3r::GUI::AgentBridge
