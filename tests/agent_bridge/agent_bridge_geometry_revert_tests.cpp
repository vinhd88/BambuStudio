// geometry_revert_rejection: when jumping Studio's undo stack back undoes only the agent's change.
#include <catch2/catch.hpp>

#include <string>
#include <vector>

#include "slic3r/GUI/AgentBridge/AgentBridgeGeometryRevertCheck.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI::AgentBridge;

namespace {
UndoRedo::Snapshot snap(const std::string &name, size_t time, UndoRedo::SnapshotType type = UndoRedo::SnapshotType::Action)
{
    UndoRedo::SnapshotData data;
    data.snapshot_type = type;
    return UndoRedo::Snapshot(name, time, 1, data);
}

UndoRedo::Snapshot topmost(size_t time) { return snap("@@@ Topmost @@@", time); }
} // namespace

TEST_CASE("geometry revert check", "[agent_bridge]")
{
    // Agent change c3 is one snapshot at time 5.
    std::vector<UndoRedo::Snapshot> stack = {snap("New Project", 1, UndoRedo::SnapshotType::ProjectSeparator), snap("Add objects", 3),
                                             snap("Agent orient_objects c3", 5)};
    const GeometrySnapshotRange     range{5, 5, "Agent orient_objects c3"};

    SECTION("nothing since: allowed")
    {
        stack.push_back(topmost(6));
        CHECK(newest_snapshot_time(stack) == 5);
        CHECK_FALSE(geometry_revert_rejection(stack, stack.back().timestamp, range).has_value());
    }
    SECTION("user clicked around: selection snapshots do not block")
    {
        stack.push_back(snap("Selection-Add", 6, UndoRedo::SnapshotType::Selection));
        stack.push_back(snap("Entering Move gizmo", 7, UndoRedo::SnapshotType::EnteringGizmo));
        stack.push_back(topmost(8));
        CHECK_FALSE(geometry_revert_rejection(stack, stack.back().timestamp, range).has_value());
    }
    SECTION("later agent changes do not block")
    {
        stack.push_back(snap("Agent settings change c4", 6));
        stack.push_back(snap("Agent revert c4", 7));
        stack.push_back(topmost(8));
        CHECK_FALSE(geometry_revert_rejection(stack, stack.back().timestamp, range).has_value());
    }
    SECTION("a user edit since blocks")
    {
        stack.push_back(snap("Delete Object", 6));
        stack.push_back(topmost(7));
        const auto reason = geometry_revert_rejection(stack, stack.back().timestamp, range);
        REQUIRE(reason.has_value());
        CHECK(reason->find("Delete Object") != std::string::npos);
    }
    SECTION("undo used back into the change blocks")
    {
        stack.push_back(topmost(6));
        CHECK(geometry_revert_rejection(stack, 5, range).has_value());
    }
    SECTION("after a newer agent change was reverted (active below the top): allowed")
    {
        stack.push_back(snap("Agent transform_object c4", 6));
        stack.push_back(topmost(7));
        CHECK_FALSE(geometry_revert_rejection(stack, 6, range).has_value());
    }
    SECTION("user edits only in the undone (redo) part do not count")
    {
        stack.push_back(snap("Agent transform_object c4", 6));
        stack.push_back(snap("Delete Object", 7));
        stack.push_back(topmost(8));
        CHECK_FALSE(geometry_revert_rejection(stack, 6, range).has_value());
        CHECK(geometry_revert_rejection(stack, 8, range).has_value());
    }
    SECTION("same timestamp, other name (history cleared and refilled) blocks")
    {
        stack[2] = snap("Move Object", 5);
        stack.push_back(topmost(6));
        CHECK(geometry_revert_rejection(stack, 6, range).has_value());
    }
    SECTION("first snapshot at the start of the history (trimmed) blocks")
    {
        stack.erase(stack.begin(), stack.begin() + 2);
        stack.push_back(topmost(6));
        CHECK(geometry_revert_rejection(stack, 6, range).has_value());
    }
    SECTION("user undid their own later step: the active snapshot itself does not count")
    {
        stack.push_back(snap("Delete Object", 6));
        stack.push_back(topmost(7));
        CHECK_FALSE(geometry_revert_rejection(stack, 6, range).has_value());
    }
    SECTION("a reverted agent change brought back with Redo blocks")
    {
        // c4 was reverted: active went to 6, and a Redo would load 7.
        stack.push_back(snap("Agent transform_object c4", 6));
        stack.push_back(topmost(7));
        const std::vector<RedoMarker> markers{{7, "@@@ Topmost @@@"}};
        CHECK_FALSE(geometry_revert_rejection(stack, 6, range, markers).has_value());
        CHECK(geometry_revert_rejection(stack, 7, range, markers).has_value());
    }
    SECTION("a redo marker dropped by a newer change does not block")
    {
        stack.push_back(snap("Agent transform_object c4", 6));
        stack.push_back(snap("Agent cut_object c5", 8));
        stack.push_back(topmost(9));
        const std::vector<RedoMarker> markers{{7, "@@@ Topmost @@@"}};
        CHECK_FALSE(geometry_revert_rejection(stack, 9, range, markers).has_value());
    }
    SECTION("snapshot dropped from history blocks")
    {
        stack.erase(stack.begin() + 2);
        stack.push_back(topmost(6));
        CHECK(geometry_revert_rejection(stack, stack.back().timestamp, range).has_value());
    }
    SECTION("Studio's own snapshots inside the change's range do not block")
    {
        stack.push_back(snap("Arrange", 6));
        stack.push_back(topmost(7));
        CHECK_FALSE(geometry_revert_rejection(stack, stack.back().timestamp, GeometrySnapshotRange{5, 6, "Agent orient_objects c3"}).has_value());
        CHECK(geometry_revert_rejection(stack, stack.back().timestamp, range).has_value());
    }
}
