// GUI-free parts of revert_change and open_in_official_studio: journal order and handoff naming.
#include <catch2/catch.hpp>

#include <ctime>
#include <memory>
#include <string>

#include "slic3r/GUI/AgentBridge/AgentBridgeChangeJournal.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgeHandoff.hpp"
#include "slic3r/GUI/AgentBridge/AgentBridgePresetSelection.hpp"
#include "libslic3r/PresetBundle.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI::AgentBridge;

TEST_CASE("ChangeJournal reverts newest first", "[agent_bridge]")
{
    ChangeJournal journal;
    journal.set_project("a");

    SECTION("nothing to revert in an empty journal")
    {
        CHECK(journal.newest_active() == nullptr);
    }
    SECTION("newest active moves back as records are reverted")
    {
        const std::string first  = journal.begin({}, {});
        const std::string second = journal.begin({}, {}, ChangeKind::FilamentPreset);
        REQUIRE(journal.newest_active()->change_id == second);
        CHECK(journal.newest_active()->kind == ChangeKind::FilamentPreset);
        journal.mark_reverted(second);
        REQUIRE(journal.newest_active()->change_id == first);
        CHECK(journal.find(second)->reverted);
        journal.mark_reverted(first);
        CHECK(journal.newest_active() == nullptr);
    }
    SECTION("a new change after a revert becomes the newest")
    {
        const std::string first = journal.begin({}, {});
        journal.mark_reverted(first);
        const std::string next = journal.begin({}, {});
        CHECK(journal.newest_active()->change_id == next);
        CHECK(journal.find(first)->reverted);
    }
    SECTION("records default to settings, with no after-values yet")
    {
        journal.begin({}, {JournalValue{"layer_height", std::make_shared<ConfigOptionFloat>(0.2), 1}});
        const ChangeRecord &record = *journal.newest_active();
        CHECK(record.kind == ChangeKind::Settings);
        CHECK(record.values[0].tab == 1);
        CHECK_FALSE(record.values[0].after_known);
        CHECK(record.restore.is_object());
    }
    SECTION("reverting an unknown id does nothing")
    {
        journal.begin({}, {});
        journal.mark_reverted("c999");
        CHECK(journal.newest_active() != nullptr);
    }
}

TEST_CASE("handoff_file_name keeps only safe characters", "[agent_bridge]")
{
    const std::time_t when = 1'790'000'000;
    const std::string name = handoff_file_name("Pen holder/../v2 é", when);
    CHECK(name.find('/') == std::string::npos);
    CHECK(name.find("..") == std::string::npos);
    CHECK(name.rfind("Pen_holder____v2_", 0) == 0);
    CHECK(name.size() > 4);
    CHECK(name.substr(name.size() - 4) == ".3mf");
    CHECK(handoff_file_name("", when).rfind("project-agent-", 0) == 0);
    CHECK(handoff_file_name(std::string(200, 'a'), when).size() < 100);
}

TEST_CASE("bundle_short_version reads an XML Info.plist", "[agent_bridge]")
{
    const std::string plist = "<dict>\n\t<key>CFBundleName</key>\n\t<string>BambuStudio</string>\n"
                              "\t<key>CFBundleShortVersionString</key>\n\t<string>02.09.01.70</string>\n</dict>";
    CHECK(bundle_short_version(plist) == std::optional<std::string>("02.09.01.70"));
    CHECK_FALSE(bundle_short_version("<dict></dict>"));
}

TEST_CASE("same_version compares numbers, not text", "[agent_bridge]")
{
    CHECK(same_version("02.09.01.70", "2.9.1.70"));
    CHECK_FALSE(same_version("02.09.01.70", "02.09.01.71"));
    CHECK_FALSE(same_version("02.09.01", "02.09.01.70"));
}

// unsaved_edits feeds select_process_preset's refusal and the before/after edits a revert compares,
// so it must list every key that makes the preset dirty.
TEST_CASE("unsaved_edits lists the keys that make a preset dirty", "[agent_bridge]")
{
    PresetBundle bundle;
    PresetCollection &prints = bundle.prints;
    REQUIRE_FALSE(prints.current_is_dirty());
    CHECK(unsaved_edits(prints).empty());

    prints.get_edited_preset().config.set_key_value("outer_wall_speed", new ConfigOptionFloats({152.}));
    prints.get_edited_preset().config.set_key_value("filter_out_gap_fill", new ConfigOptionFloat(0.5));
    prints.get_edited_preset().config.set_key_value("print_settings_id", new ConfigOptionString("some other preset"));
    REQUIRE(prints.current_is_dirty());
    const auto edits = unsaved_edits(prints);
    CHECK(edits.contains("outer_wall_speed"));
    CHECK(edits.contains("filter_out_gap_fill"));

    const auto rejection = unsaved_changes_rejection(prints, "The process preset");
    CHECK(rejection["code"] == "unsaved_changes");
    CHECK(rejection["unsaved_keys"] == nlohmann::json::array({"filter_out_gap_fill", "outer_wall_speed"}));
}
