// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/tutorial_guide.hpp"

#include "ui/desktop.hpp"
#include "ui/font_set.hpp"
#include "ui/game_style.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    constexpr Rect kView { 0, 0, 1024, 768 };

    save::TutorialScript lessons()
    {
        save::TutorialScript s;
        s.name = "Lessons";
        s.title = "Looking about";
        s.steps.push_back({ "Step 1", "Look around.", { }, 0, 0 });
        s.steps.push_back({ "Step 2", "Build.", { }, 1, 400 });
        s.steps.push_back({ "Step 3", "Done.", { }, 2, 281 });
        return s;
    }

    // Letters 10 wide, spaces 5, lines 20 apart.
    Font test_font()
    {
        ovl::Font f;
        f.metrics = { 0.1f, 1.5f, 0.5f };
        f.has_code_table = true;
        for (char16_t c = u'a'; c <= u'z'; ++c)
            f.glyphs.push_back({ c, 1, 0, { 0, 0, 0.1f, 0.1f }, { 0, 0 }, 0 });
        f.glyphs.push_back({ u' ', 0.5f, 0, { 0, 1, 0, 1 }, { 0, 0 }, 0 });
        const std::array<Font::Page, 1> pages = { { { 3, 100, 100 } } };
        return Font::from(f, pages);
    }

    PointerState at(float x, float y, bool down = false)
    {
        return { { x, y }, down, true };
    }

    struct Fixture
    {
        StringTable strings;
        FontSet fonts;
        Desktop desktop;
        TutorialGuide* window = nullptr;

        explicit Fixture(save::TutorialScript script = lessons())
        {
            fonts.set(FontRole::Small, test_font());
            strings.add("TutorialText_Lessons_Step1_Text", u"Regardez.");
            strings.add("TutorialWindow_Continue", u"On");
            window = &desktop.root().add<TutorialGuide>("tutorial", game_skins::tutorial_guide_style(), strings, std::move(script));
            desktop.layout(kView);
            update();
        }

        void update(PointerState pointer = { })
        {
            desktop.update(pointer, { 0, &fonts });
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

TEST_CASE("the tutorial's name is the language's where the strings have it, else the park's")
{
    StringTable strings;
    const save::TutorialScript s = lessons();
    CHECK(tutorial_title(strings, s) == u"Looking about");
    strings.add("TutorialText_Lessons_Title", u"Regarder");
    CHECK(tutorial_title(strings, s) == u"Regarder");
}

TEST_CASE("a step's stored place: the top-right or bottom-right corner, else the middle")
{
    CHECK(tutorial_guide_place(0) == TutorialGuidePlace::Centre);
    CHECK(tutorial_guide_place(1) == TutorialGuidePlace::TopRight);
    CHECK(tutorial_guide_place(2) == TutorialGuidePlace::BottomRight);
    CHECK(tutorial_guide_place(3) == TutorialGuidePlace::Centre);
    CHECK(tutorial_guide_place(-7) == TutorialGuidePlace::Centre);
}

TEST_CASE("the page is at least the floor wide and tall, and grows with the text")
{
    TutorialGuideStyle style;
    style.window.content_inset = { 8, 70, 13, 13 };
    style.min_width = 282;
    style.min_page_width = 281;
    style.min_page_height = 220;
    style.text_top = 5;
    style.text_gap = 15;
    style.button_size = { 90, 30 };

    SUBCASE("no stored width, or a nonsense one, gives the floor")
    {
        for (const float stored : { 0.0f, -1000.0f, 100.0f })
        {
            const TutorialGuideSize s = tutorial_guide_size(style, stored, 20);
            CHECK(s.page.x == doctest::Approx(281));
            CHECK(s.page.y == doctest::Approx(220));
            CHECK(s.window.x == doctest::Approx(281 + 21));
            CHECK(s.window.y == doctest::Approx(220 + 83));
        }
    }
    SUBCASE("a wider page and longer text")
    {
        const TutorialGuideSize s = tutorial_guide_size(style, 400, 300);
        CHECK(s.page.x == doctest::Approx(400));
        CHECK(s.page.y == doctest::Approx(5 + 300 + 15 + 30));
        CHECK(s.window.x == doctest::Approx(421));
        CHECK(s.window.y == doctest::Approx(350 + 83));
    }
    SUBCASE("the window never gets narrower than its own floor")
    {
        style.min_page_width = 200;
        CHECK(tutorial_guide_size(style, 0, 0).window.x == doctest::Approx(282));
    }
    CHECK(tutorial_guide_text_width(TutorialGuideStyle { }, 281) == doctest::Approx(275));
}

TEST_CASE("the corners keep the margin from the desktop's edges")
{
    TutorialGuideStyle style;
    style.screen_margin = 24;
    const Point size { 300, 400 };
    CHECK(place(tutorial_guide_placement(style, TutorialGuidePlace::Centre, size), kView) == Rect { 362, 184, 662, 584 });
    CHECK(place(tutorial_guide_placement(style, TutorialGuidePlace::TopRight, size), kView) == Rect { 700, 24, 1000, 424 });
    CHECK(place(tutorial_guide_placement(style, TutorialGuidePlace::BottomRight, size), kView) == Rect { 700, 344, 1000, 744 });
}

TEST_CASE("the window shows the tutorial's name, the step line and the text, with no close button")
{
    Fixture f;
    CHECK(f.window->title == u"Looking about");
    CHECK(f.window->step_line().text == u"Step 1");
    CHECK(f.window->text().text == u"Regardez.");
    CHECK(f.window->continue_button().text == u"On");
    CHECK_FALSE(f.window->find("close")->visible);
    CHECK(f.window->icon == game_skins::kTutorialGuide);

    const TutorialGuideStyle style = game_skins::tutorial_guide_style();
    const Rect page = f.window->content().rect();
    const Rect quit = f.window->quit_button().rect();
    const Rect go = f.window->continue_button().rect();
    CHECK(quit == Rect { page.x0, page.y1 - 30, page.x0 + 90, page.y1 });
    CHECK(go == Rect { page.x1 - 90, page.y1 - 30, page.x1, page.y1 });
    CHECK(f.window->text().rect().y0 == doctest::Approx(page.y0 + style.text_top));
    // The step line sits between the title rule and the page.
    const Rect line = f.window->step_line().rect();
    CHECK(line.y0 == doctest::Approx(f.window->rect().y0 + style.step_line.y0));
    CHECK(line.y1 <= page.y0);
}

TEST_CASE("each step goes to its own place, and stays put within the step")
{
    Fixture f;
    const TutorialGuideStyle style = game_skins::tutorial_guide_style();

    // Step one goes in the middle at the floor size.
    const TutorialGuideSize first = tutorial_guide_size(style, 0, 20);
    Rect r = f.window->rect();
    CHECK(r.width() == doctest::Approx(first.window.x));
    CHECK(r.height() == doctest::Approx(first.window.y));
    CHECK(r.x0 + r.x1 == doctest::Approx(1024));
    CHECK(r.y0 + r.y1 == doctest::Approx(768));

    SUBCASE("updates do not move it")
    {
        f.update();
        f.update();
        CHECK(f.window->rect() == r);
    }
    SUBCASE("a drag holds within the step")
    {
        f.update(at(r.x0 + 100, r.y0 + 5, true));
        f.update(at(r.x0 + 60, r.y0 + 45, true));
        f.update(at(r.x0 + 60, r.y0 + 45, false));
        f.update();
        CHECK(f.window->rect().x0 == doctest::Approx(r.x0 - 40));
        CHECK(f.window->rect().y0 == doctest::Approx(r.y0 + 40));
    }

    // Step two asks for the top-right corner and a wider page.
    f.window->continue_button().click();
    f.update();
    CHECK(f.window->step() == 1);
    CHECK(f.window->step_line().text == u"Step 2");
    r = f.window->rect();
    CHECK(r.width() == doctest::Approx(400 + 21));
    CHECK(r.x1 == doctest::Approx(1024 - style.screen_margin));
    CHECK(r.y0 == doctest::Approx(style.screen_margin));

    // The last step, bottom-right, has no Continue.
    f.window->continue_button().click();
    f.update();
    CHECK(f.window->step() == 2);
    CHECK_FALSE(f.window->continue_button().visible);
    CHECK(f.window->rect().y1 == doctest::Approx(768 - style.screen_margin));
}

TEST_CASE("a long text makes the window taller, keeping it in its place")
{
    save::TutorialScript s = lessons();
    s.name = "Long"; // Not the name the fixture's strings replace step one's text under
    for (int i = 0; i < 40; ++i)
        s.steps[0].text += "word word word word ";
    Fixture f(std::move(s));
    const Rect r = f.window->rect();
    CHECK(r.height() > tutorial_guide_size(game_skins::tutorial_guide_style(), 0, 0).window.y);
    CHECK(r.y0 + r.y1 == doctest::Approx(768));
}

TEST_CASE("quit and Escape ask to leave, and the window stays")
{
    Fixture f;
    CHECK_FALSE(f.window->take_quit());
    f.window->quit_button().click();
    f.update();
    CHECK(f.window->take_quit());
    CHECK_FALSE(f.window->take_quit());
    CHECK(f.desktop.root().find("tutorial") != nullptr);
    CHECK(f.window->cancel_button == &f.window->quit_button());
}
