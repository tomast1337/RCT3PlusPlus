// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/staff_roster.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/person_info.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;
using namespace rct3::ui::screens;

namespace
{
    struct Fixture
    {
        Desktop desktop;
        StringTable strings;
        save::model::PeopleModel people;
        StaffRoster* window = nullptr;

        Fixture()
        {
            for (const save::model::StaffKind kind : { save::model::StaffKind::Handyman, save::model::StaffKind::Mechanic, save::model::StaffKind::ParkInspector })
            {
                save::model::Staff s;
                s.kind = kind;
                people.staff.push_back(s);
            }
            window = &desktop.root().add<StaffRoster>("Staff_Title", game_skins::staff_roster_style(kGamePalette), strings, 520, 420);
            show();
        }

        void show(bool animal_houses = false)
        {
            window->show({ .people = &people, .animal_houses = animal_houses }, strings, { });
            desktop.layout({ 0, 0, 1024, 768 });
            desktop.update({ });
        }

        Button& button(std::string_view path)
        {
            auto b = window->content().find_as<Button>(path);
            REQUIRE(b);
            return *b;
        }
    };
} // namespace

TEST_CASE("a row unfolds and folds up on its box, and stays unfolded when the list is written again")
{
    Fixture f;
    CHECK(f.window->rows() == std::vector<std::pair<std::size_t, bool>> { { 0, false }, { 1, false }, { 2, false } });
    f.button("table/view/content/row1/Expand").click();
    f.desktop.update({ });
    CHECK(f.window->rows()[1].second);
    // The mechanic's tasks show unfolded.
    CHECK(f.window->content().find("table/view/content/row1/Staff_TaskButtonRepair"));
    f.people.staff[0].happiness = 0.1f;
    f.show();
    CHECK(f.window->rows()[1].second);
    f.button("table/view/content/row1/Expand").click();
    f.desktop.update({ });
    CHECK_FALSE(f.window->rows()[1].second);
}

TEST_CASE("the window's plus and minus unfold and fold every row")
{
    Fixture f;
    f.button("ExpandAll").click();
    f.desktop.update({ });
    for (const auto& [index, open] : f.window->rows())
        CHECK(open);
    f.button("ContractAll").click();
    f.desktop.update({ });
    for (const auto& [index, open] : f.window->rows())
        CHECK_FALSE(open);
}

TEST_CASE("a job's switch hides its workers and the drop-down sorts the rows")
{
    Fixture f;
    f.button("Staff_ViewMaintenance").click();
    f.desktop.update({ });
    f.show();
    CHECK(f.window->rows().size() == 2);
    CHECK_FALSE(f.window->content().find("table/view/content/row0"));
    auto sort = f.window->content().find_as<Dropdown>("SortBy");
    REQUIRE(sort);
    CHECK(sort->items.size() == kRosterSorts);
}

TEST_CASE("hiring a keeper with no animal house says an enclosure is needed instead")
{
    Fixture f;
    f.button("Staff_HireAnimalKeeperIcon").click();
    f.desktop.update({ });
    CHECK_FALSE(f.window->take_asks().hire);
    CHECK(f.desktop.root().find("StaffQuestion"));
    f.show(true);
    f.button("Staff_HireAnimalKeeperIcon").click();
    f.desktop.update({ });
    CHECK(f.window->take_asks().hire == save::model::StaffKind::AnimalKeeper);
    f.button("Staff_HireMechanicIcon").click();
    f.desktop.update({ });
    CHECK(f.window->take_asks().hire == save::model::StaffKind::Mechanic);
}

TEST_CASE("an unfolded row's switches and arrows ask for the worker's tasks and wage")
{
    Fixture f;
    f.people.staff[0].wage_level = 1;
    f.show();
    f.button("table/view/content/row0/Expand").click();
    f.desktop.update({ });
    f.button("table/view/content/row0/Staff_TaskButtonBins").click();
    f.desktop.update({ });
    auto asks = f.window->take_asks();
    REQUIRE(asks.change);
    CHECK(asks.change->first == 0);
    CHECK(asks.change->second.id == person_control(PersonControl::Task, 2));
    f.button("table/view/content/row0/WageUp").click();
    f.desktop.update({ });
    asks = f.window->take_asks();
    REQUIRE(asks.change);
    CHECK(asks.change->second.id == person_control(PersonControl::Wage));
    CHECK(asks.change->second.value == 2);
}

TEST_CASE("the inspector's row unfolds into the kinds of things they look at, with no wage")
{
    Fixture f;
    f.button("table/view/content/row2/Expand").click();
    f.desktop.update({ });
    CHECK(f.window->content().find("table/view/content/row2/Staff_TaskButtonEvalCoasters"));
    CHECK_FALSE(f.window->content().find("table/view/content/row2/WageUp"));
    CHECK_FALSE(f.window->content().find("table/view/content/row2/Patrol"));
    CHECK_FALSE(f.window->content().find("table/view/content/row2/Mood"));
}

TEST_CASE("a handyman's row has the mend switch, lit while they mend")
{
    Fixture f;
    f.people.staff[0].task_flags = 0;
    f.show();
    f.button("table/view/content/row0/Expand").click();
    f.desktop.update({ });
    Button& mend = f.button("table/view/content/row0/Staff_TaskButtonRepair");
    CHECK(mend.on); // The bit set means they leave broken things be
    f.people.staff[0].task_flags = 1 << 3;
    f.show();
    CHECK_FALSE(f.button("table/view/content/row0/Staff_TaskButtonRepair").on);
    f.button("table/view/content/row0/Staff_TaskButtonRepair").click();
    f.desktop.update({ });
    const auto asks = f.window->take_asks();
    REQUIRE(asks.change);
    CHECK(asks.change->second.id == person_control(PersonControl::Task, 3));
}

TEST_CASE("a row's patrol button is held down while their area is painted")
{
    Fixture f;
    CHECK_FALSE(f.button("table/view/content/row0/Patrol").kept_down);
    const std::array<StaffLook, 1> looks { StaffLook { .painting_patrol = true } };
    f.window->show({ .people = &f.people, .staff_looks = looks }, f.strings, { });
    f.desktop.update({ });
    CHECK(f.button("table/view/content/row0/Patrol").kept_down);
}
