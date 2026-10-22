// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/front_end.hpp"

#include "ui/game_style.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    FrontEndStyle style()
    {
        return { game_skins::options_dialog_style(kGamePalette), game_skins::question_box_style(kGamePalette), game_skins::file_dialog_style(kGamePalette), game_skins::career_dialog_style(kGamePalette), game_skins::career_board_style(kGamePalette), game_skins::tutorial_picker_style(kGamePalette) };
    }

    struct Fixture
    {
        StringTable strings;
        config::GameOptions options;
        Desktop desktop;
        FrontEnd front;

        explicit Fixture(FrontEndSetup setup = { })
            : front(desktop, style(), strings, options, std::move(setup))
        {
            desktop.layout({ 0, 0, 1024, 768 });
            step();
        }
        void step()
        {
            desktop.update({ });
            front.update();
        }
        void press(std::optional<FrontButton&> b)
        {
            REQUIRE(b);
            b->click();
            step();
        }
    };
} // namespace

TEST_CASE("options opens its window once; closing it is the time to save")
{
    Fixture f;
    CHECK(!f.front.options_window());
    f.press(f.front.screen().button(FrontMenu::Main, 2));
    REQUIRE(f.front.options_window());
    f.press(f.front.screen().button(FrontMenu::Main, 2));
    CHECK(f.desktop.root().children().size() == 2); // The screen and one window
    CHECK_FALSE(f.front.take_options_closed());

    f.front.options_window()->close();
    f.step();
    CHECK(!f.front.options_window());
    CHECK(f.front.take_options_closed());
    CHECK_FALSE(f.front.take_options_closed());
}

TEST_CASE("quitting asks first")
{
    Fixture f;
    f.press(f.front.screen().quit_button());
    REQUIRE(f.front.question());
    CHECK_FALSE(f.front.take_quit());

    SUBCASE("no stays")
    {
        KeyboardState escape;
        escape.presses.push_back({ Key::Escape });
        f.desktop.update({ }, escape);
        f.front.update();
        CHECK(!f.front.question());
        CHECK_FALSE(f.front.take_quit());
    }
    SUBCASE("yes quits")
    {
        KeyboardState enter;
        enter.presses.push_back({ Key::Enter });
        f.desktop.update({ }, enter);
        f.front.update();
        CHECK(f.front.take_quit());
    }
}

TEST_CASE("choices nothing carries out yet go to the owner")
{
    Fixture f;
    f.front.screen().open(FrontMenu::Tools);
    f.step();
    f.press(f.front.screen().button(FrontMenu::Tools, 2));
    CHECK(f.front.take_unhandled() == FrontRequest { FrontAction::CoasterDesigner, 0 });
    CHECK_FALSE(f.front.take_unhandled());
}

TEST_CASE("the options window can be open from the start")
{
    FrontEndSetup setup;
    setup.open_options = true;
    Fixture f(setup);
    CHECK(f.front.options_window());
}

TEST_CASE("loading opens the window that loads games on the right folder, once")
{
    Fixture f;
    f.front.screen().open(FrontMenu::Sandbox);
    f.step();
    f.press(f.front.screen().button(FrontMenu::Sandbox, 1));
    auto parks = f.front.file_dialog();
    REQUIRE(parks);
    CHECK(parks->mode() == FileDialogMode::Load);
    CHECK(f.front.file_folder() == config::UserFolder::Parks);
    CHECK(f.front.file_action() == FrontAction::LoadSandbox);
    CHECK(f.front.file_dialogs_opened() == 1);
    CHECK_FALSE(f.front.take_unhandled());

    // The same choice again keeps the window; another folder replaces it.
    f.press(f.front.screen().button(FrontMenu::Sandbox, 1));
    CHECK(f.front.file_dialogs_opened() == 1);
    f.front.screen().open(FrontMenu::CustomScenario);
    f.step();
    f.press(f.front.screen().button(FrontMenu::CustomScenario, 0));
    CHECK(f.front.file_folder() == config::UserFolder::NewScenarios);
    CHECK(f.front.file_action() == FrontAction::NewCustomScenario);
    CHECK(f.front.file_dialogs_opened() == 2);
}

TEST_CASE("a game's career button opens the window picking its careers")
{
    Fixture f;
    f.front.screen().open(FrontMenu::Campaigns);
    f.step();
    f.press(f.front.screen().button(FrontMenu::Campaigns, 2));
    REQUIRE(f.front.career_dialog());
    CHECK(f.front.career_pack() == 2);
    CHECK(f.front.career_dialogs_opened() == 1);
    CHECK_FALSE(f.front.take_unhandled());
    f.press(f.front.screen().button(FrontMenu::Campaigns, 2));
    CHECK(f.front.career_dialogs_opened() == 1);
    f.press(f.front.screen().button(FrontMenu::Campaigns, 0));
    CHECK(f.front.career_pack() == 0);
    CHECK(f.front.career_dialogs_opened() == 2);
}

TEST_CASE("the career map stands in for the front screen, and going back shows the games' menu")
{
    Fixture f;
    f.front.screen().open(FrontMenu::Campaigns);
    f.step();
    f.press(f.front.screen().button(FrontMenu::Campaigns, 0));
    REQUIRE(f.front.career_dialog());
    std::vector<CareerBoardScenario> scenarios(3);
    scenarios[0].status = career::ScenarioStatus::Open;
    f.front.open_career_board(std::move(scenarios), 0);
    CHECK(!f.front.career_dialog());
    REQUIRE(f.front.career_board());
    CHECK(f.front.career_boards_opened() == 1);
    CHECK_FALSE(f.front.screen().visible);

    f.front.career_board()->quit_button().click();
    f.step();
    REQUIRE(f.front.question());
    f.front.question()->buttons().back()->click();
    f.step();
    f.step();

    f.front.career_board()->menu_button().click();
    f.step();
    REQUIRE(f.front.career_board()->question());
    f.front.career_board()->question()->buttons().front()->click();
    f.step();
    f.step();
    CHECK(!f.front.career_board());
    CHECK(f.front.screen().visible);
    CHECK(f.front.screen().current() == FrontMenu::Campaigns);
}

TEST_CASE("a game's tutorial button shows its tutorials until the menu of games is left")
{
    FrontEndSetup setup;
    setup.expansions = { true, true };
    Fixture f(setup);
    f.strings.add("TutorialList_Name1", u"Looking round");
    f.strings.add("Tutorial_AOP0_List_Name1", u"Pools");
    f.strings.add("Tutorial_AOP0_List_Name2", u"Slides");
    f.front.screen().open(FrontMenu::Tutorials);
    f.step();
    f.press(f.front.screen().button(FrontMenu::Tutorials, 0));
    REQUIRE(f.front.tutorial_picker());
    CHECK(f.front.tutorial_picker()->size() == 1);
    CHECK(f.front.tutorial_pickers_opened() == 1);
    CHECK_FALSE(f.front.take_unhandled());

    // Under the screen's windows: the screen first, then the menu.
    CHECK(f.desktop.root().children().at(1).get() == rct3::as_pointer(f.front.tutorial_picker()));

    f.press(f.front.screen().button(FrontMenu::Tutorials, 0));
    CHECK(f.front.tutorial_pickers_opened() == 1);
    f.press(f.front.screen().button(FrontMenu::Tutorials, 1));
    CHECK(f.front.tutorial_pack() == 1);
    REQUIRE(f.front.tutorial_picker());
    CHECK(f.front.tutorial_picker()->size() == 2);
    CHECK(f.front.tutorial_pickers_opened() == 2);

    f.front.screen().back();
    f.step();
    CHECK(!f.front.tutorial_picker());
}
