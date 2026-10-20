// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/career_board.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;
using career::ScenarioStatus;

namespace
{
    struct Fixture
    {
        StringTable strings;
        Desktop desktop;
        CareerBoard* map = nullptr;

        explicit Fixture(std::size_t picked = 1)
        {
            strings.add("CareerMap_ScenarioDD", u"No. %NUMBER%");
            strings.add("CareerMap_MustCompleteNLevels", u"Finish %NUMBER% more");
            strings.add("CareerMap_MustCompleteTycoon", u"Tycoon everywhere");
            strings.add("CareerMap_NewGame_TT", u"Begin");
            strings.add("CareerMap_Message_ConfirmNew", u"Begin now?");
            strings.add("CareerMap_Title_ConfirmNew", u"Begin");
            career::Career c = career::new_career("A", 1, std::vector<std::string> { "Hill", "Lake", "Mine", "Dune", "Reef", "Cave", "Peak" });
            c.scenarios[0].status = ScenarioStatus::Tycoon;
            career::open_earned(c);
            std::vector<CareerBoardScenario> scenarios;
            for (std::size_t i = 0; i < c.scenarios.size(); ++i)
            {
                CareerBoardScenario s;
                s.info.id = c.scenarios[i].id;
                s.info.title = u"Title " + std::u16string(1, static_cast<char16_t>(u'A' + i));
                s.status = c.scenarios[i].status;
                s.lock = career::lock(c, i);
                s.can_resume = i == 0;
                scenarios.push_back(std::move(s));
            }
            map = &desktop.root().add<CareerBoard>("map", game_skins::career_board_style(), strings, std::move(scenarios), picked);
            desktop.layout({ 0, 0, 1024, 768 });
            tick();
        }

        void tick(float seconds = 0)
        {
            desktop.update({ }, { }, { seconds, nullptr });
        }
        void hover(const Widget& w)
        {
            const Rect r = w.rect();
            desktop.update({ { (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2 } });
        }
        void answer(bool yes)
        {
            auto q = map->question();
            REQUIRE(q);
            (yes ? q->buttons().front() : q->buttons().back())->click();
            tick();
            tick();
        }
    };
} // namespace

TEST_CASE("the scenarios sit on an arc, the picked one larger, locked ones disabled")
{
    Fixture f;
    CHECK(f.map->picked() == 1);
    CHECK(f.map->title() == u"Title B");
    const Rect picked = f.map->scenario_button(1).rect();
    const Rect other = f.map->scenario_button(0).rect();
    CHECK(picked.width() == doctest::Approx(other.width() * career_board::kPickedFactor));
    CHECK(other.y0 < picked.y0); // Further down the arc
    CHECK(f.map->scenario_button(3).enabled);
    CHECK_FALSE(f.map->scenario_button(4).enabled);
    CHECK_FALSE(f.map->resume_button().visible);
}

TEST_CASE("a locked scenario cannot be shown first")
{
    Fixture f(5);
    CHECK(f.map->picked() == 0);
    CHECK(f.map->resume_button().visible);
}

TEST_CASE("picking a scenario shows it and tells the owner")
{
    Fixture f;
    f.map->scenario_button(0).click();
    f.tick();
    CHECK(f.map->picked() == 0);
    CHECK(f.map->take_picked() == 0);
    CHECK_FALSE(f.map->take_picked());
    CHECK(f.map->resume_button().visible);
    f.map->scenario_button(0).click(); // Already shown
    f.tick();
    CHECK_FALSE(f.map->take_picked());
}

TEST_CASE("new asks first")
{
    Fixture f;
    f.map->new_button().click();
    f.tick();
    REQUIRE(f.map->question());
    CHECK(f.map->question()->text().text == u"Begin now?");
    CHECK(f.map->question()->title == u"Begin");
    f.answer(false);
    CHECK_FALSE(f.map->take_new());
    f.map->new_button().click();
    f.tick();
    f.answer(true);
    CHECK(f.map->take_new() == 1);
    CHECK_FALSE(f.map->take_new());
}

TEST_CASE("escape asks to go back to the main menu; load and quit go to the owner at once")
{
    Fixture f;
    KeyboardState keys;
    keys.presses = { { Key::Escape } };
    f.desktop.update({ }, keys);
    f.tick();
    f.answer(true);
    CHECK(f.map->take_menu());
    f.map->load_button().click();
    f.map->quit_button().click();
    f.tick();
    CHECK(f.map->take_load());
    CHECK(f.map->take_quit());
}

TEST_CASE("the bottom strip names the picked scenario, the one under the pointer, or what a locked one waits for")
{
    Fixture f;
    CHECK(f.map->help_title() == u"No. 2");
    CHECK(f.map->help_text() == u"Title B");
    f.hover(f.map->scenario_button(4));
    CHECK(f.map->help_title() == u"No. 5");
    CHECK(f.map->help_text() == u"Finish 1 more");
    f.hover(f.map->scenario_button(6));
    CHECK(f.map->help_text() == u"Tycoon everywhere");
    f.hover(f.map->new_button());
    CHECK(f.map->help_title() == u"Begin");
}

TEST_CASE("the front piece of the large picture settles in a third of a second")
{
    CHECK(career_board::pop(0) == doctest::Approx(career_board::kPopFactor));
    CHECK(career_board::pop(0.3f) == doctest::Approx(1.0f));
    CHECK(career_board::pop(1) == doctest::Approx(1.0f));
    CHECK(career_board::pop(0.15f) < 1.05f);
}
