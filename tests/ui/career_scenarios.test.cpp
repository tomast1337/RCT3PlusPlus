// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/career_scenarios.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("each game's career scenarios come from its keys, up to the first missing")
{
    StringTable strings;
    strings.add("Scenario01_Lookup", u"Meadow");
    strings.add("Scenario02_Lookup", u"Canyon");
    strings.add("Scenario04_Lookup", u"Skipped");
    strings.add("AOP1_Scenario01_Lookup", u"Savanna");
    CHECK(career_scenario_ids(strings, 0) == std::vector<std::string> { "Meadow", "Canyon" });
    CHECK(career_scenario_ids(strings, 1).empty());
    CHECK(career_scenario_ids(strings, 2) == std::vector<std::string> { "Savanna" });
}

TEST_CASE("a new career is named in the language, with a number")
{
    StringTable strings;
    strings.add("PlayerProfile_Player", u"Joueur");
    CHECK(default_career_name(strings, 3) == u"Joueur 3");
}

TEST_CASE("a scenario's text, pictures and folders come from its id and place")
{
    StringTable strings;
    strings.add("ScenarioMeadow_Title", u"The Meadow");
    strings.add("ScenarioMeadow_Text", u"Grass.");
    strings.add("Meadow_Filename", u"Field");
    strings.add("AOP_FSName_1", u"Second");
    const CareerScenarioInfo base = career_scenario(strings, 0, 2, "Meadow");
    CHECK(base.title == u"The Meadow");
    CHECK(base.description == u"Grass.");
    CHECK(base.icon == "ScenarioIcon_Meadow");
    CHECK(base.picture == "CareerMap_LargeImage3");
    CHECK(base.picture_front == "CareerMap_LargeImageForeground3");
    CHECK(base.slideshow == "Slideshow/Scenario3");
    CHECK(base.file_part == "Field");
    const CareerScenarioInfo more = career_scenario(strings, 2, 0, "Meadow");
    CHECK(more.picture == "CareerMap_LargeImage_Meadow");
    CHECK(more.picture_front == "CareerMap_LargeImageFG_Meadow");
    CHECK(more.slideshow == "Slideshow/Second/Scenario1");
    CHECK(career_park_folder(strings, 0) == "Campaigns/Base");
    CHECK(career_park_folder(strings, 2) == "Campaigns/Base/Second");
}

TEST_CASE("a scenario's park is the first file whose name holds its part, ignoring case")
{
    const std::vector<std::filesystem::path> files { "a/Blank.dat", "a/BigFIELDS.dat", "a/Field.dat" };
    CHECK(find_career_park(files, "field") == 1);
    CHECK_FALSE(find_career_park(files, "Lake"));
    CHECK_FALSE(find_career_park(files, ""));
}

TEST_CASE("a locked design's help names the scenario and the level that open it")
{
    StringTable strings;
    strings.add("TrackBrowser_LockedHelp", u"Reach %LEVEL% in %SCENARIO%.");
    strings.add("Scenario01_Lookup", u"Meadow");
    strings.add("Scenario02_Lookup", u"Canyon");
    strings.add("ScenarioCanyon_Title", u"Red Canyon");
    strings.add("Difficulty_Entrepreneur", u"Entrepreneur");
    CHECK(locked_design_help(strings, 1, 1) == u"Reach Entrepreneur in Red Canyon.");
    CHECK(locked_design_help(strings, 5, 7) == u"Reach  in .");
}
