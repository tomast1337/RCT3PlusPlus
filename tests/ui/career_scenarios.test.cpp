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
