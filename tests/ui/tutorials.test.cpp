// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/tutorials.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    StringTable eight_tutorials()
    {
        StringTable strings;
        for (int n = 1; n <= 8; ++n)
        {
            const std::string number = std::to_string(n);
            strings.add("TutorialList_Name" + number, u"Lesson");
        }
        return strings;
    }
} // namespace

TEST_CASE("a game's tutorials come from its keys, up to the first missing")
{
    StringTable strings;
    strings.add("TutorialList_Name1", u"Looking round");
    strings.add("TutorialList_Description1", u"Move the view.");
    strings.add("TutorialIcon1", u"Eye");
    strings.add("TutorialList_Name3", u"Skipped");
    strings.add("Tutorial_AOP1_List_Name1", u"Pens");
    strings.add("Tutorial_AOP1_Icon1", u"Fence");
    const std::array<bool, 2> none { };

    const auto base = tutorials(strings, 0, none);
    REQUIRE(base.size() == 1);
    CHECK(base[0].name == u"Looking round");
    CHECK(base[0].description == u"Move the view.");
    CHECK(base[0].icon == "Eye");
    CHECK(base[0].park == "Tutorials/Tutorial1.dat");

    CHECK(tutorials(strings, 1, none).empty());
    const auto second = tutorials(strings, 2, none);
    REQUIRE(second.size() == 1);
    CHECK(second[0].icon == "Fence");
    CHECK(second[0].park == "Tutorials/Tutorial_AOP1_1.dat");
}

TEST_CASE("the expansions change the original game's extras")
{
    const StringTable strings = eight_tutorials();
    SUBCASE("alone it has its own park")
    {
        const auto list = tutorials(strings, 0, std::array<bool, 2> { false, false });
        REQUIRE(list.size() == 8);
        CHECK(list[7].park == "Tutorials/Tutorial8.dat");
    }
    SUBCASE("the first expansion gives it another")
    {
        const auto list = tutorials(strings, 0, std::array<bool, 2> { true, false });
        REQUIRE(list.size() == 8);
        CHECK(list[7].park == "Tutorials/Tutorial8_Alternate.dat");
        CHECK(list[6].park == "Tutorials/Tutorial7.dat");
    }
    SUBCASE("the second leaves it out")
    {
        CHECK(tutorials(strings, 0, std::array<bool, 2> { true, true }).size() == 7);
        CHECK(tutorials(strings, 0, std::array<bool, 2> { false, true }).size() == 7);
    }
}
