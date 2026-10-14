// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/tutorial_guide.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    save::TutorialScript lessons()
    {
        save::TutorialScript s;
        s.name = "Lessons";
        s.steps.push_back({ "Step 1", "Look around.", { }, 0, 0 });
        s.steps.push_back({ "Step 2", "Build.", { }, 1, 400 });
        return s;
    }

    struct Fixture
    {
        StringTable strings;
        Desktop desktop;
        TutorialGuide* window = nullptr;

        Fixture()
        {
            strings.add("TutorialText_Lessons_Step1_Text", u"Regardez.");
            strings.add("TutorialWindow_Continue", u"On");
            window = &desktop.root().add<TutorialGuide>("tutorial", game_skins::question_box_style(), strings, lessons());
            desktop.layout({ 0, 0, 1024, 768 });
            desktop.update({ });
        }
    };
} // namespace

TEST_CASE("a step's text is the language's where the strings have it, else the park's")
{
    StringTable strings;
    strings.add("TutorialText_Lessons_Step2_StepTitle", u"Deux");
    const save::TutorialScript s = lessons();
    const TutorialStepText first = tutorial_step_text(strings, s, 0);
    CHECK(first.title == u"Step 1");
    CHECK(first.text == u"Look around.");
    CHECK(tutorial_step_text(strings, s, 1).title == u"Deux");
    CHECK(tutorial_step_text(strings, s, 5).text.empty());
}

TEST_CASE("continue walks through the steps; the last has none")
{
    Fixture f;
    CHECK(f.window->step() == 0);
    CHECK(f.window->text().text == u"Regardez.");
    CHECK(f.window->title == u"Step 1");
    CHECK(f.window->continue_button().text == u"On");
    CHECK(f.window->rect().x1 == doctest::Approx(1024 - tutorial_guide::kMargin));

    f.window->continue_button().click();
    f.desktop.update({ });
    CHECK(f.window->step() == 1);
    CHECK(f.window->text().text == u"Build.");
    CHECK_FALSE(f.window->continue_button().visible);
    CHECK(f.window->rect().width() == doctest::Approx(400));
}

TEST_CASE("quit and the close button ask to leave, and the window stays")
{
    Fixture f;
    CHECK_FALSE(f.window->take_quit());
    f.window->quit_button().click();
    f.desktop.update({ });
    CHECK(f.window->take_quit());
    CHECK_FALSE(f.window->take_quit());
    CHECK(f.desktop.root().find("tutorial") != nullptr);
}
