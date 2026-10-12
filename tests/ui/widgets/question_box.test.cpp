// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/question_box.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    // A 200 by 100 box in a 400 by 300 view: from (100, 100), its content inset by 10 all round, so the
    // buttons, 50 by 20 with 10 between, sit on y 170 to 190 around x 200.
    QuestionBoxStyle style()
    {
        QuestionBoxStyle s;
        s.window.content_inset = { 10, 10, 10, 10 };
        s.button = "Btn";
        s.width = 200;
        s.height = 100;
        s.button_width = 50;
        s.button_height = 20;
        s.button_gap = 10;
        return s;
    }

    PointerState at(float x, float y, bool down = false)
    {
        return { { x, y }, down, true };
    }

    struct Fixture
    {
        Desktop desktop;
        Widget& below = desktop.root().add<Widget>("below");
        QuestionBox& box = desktop.root().add<QuestionBox>("box", style(), u"Quit", u"Leave the park?", std::vector<std::u16string> { u"Yes", u"Save", u"No" });

        Fixture()
        {
            desktop.layout({ 0, 0, 400, 300 });
        }

        void keys(std::initializer_list<Key> list)
        {
            KeyboardState k;
            for (Key key : list)
                k.presses.push_back({ key });
            desktop.update(at(0, 0), k);
        }
    };
} // namespace

TEST_CASE("a question box is modal, with its answers centred below the text")
{
    Fixture f;
    CHECK(f.box.modal);
    CHECK(f.box.title == u"Quit");
    CHECK(f.box.text().text == u"Leave the park?");
    REQUIRE(f.box.buttons().size() == 3);
    CHECK(f.box.buttons()[0]->rect() == Rect { 115, 170, 165, 190 });
    CHECK(f.box.buttons()[1]->rect() == Rect { 175, 170, 225, 190 });
    CHECK(f.box.buttons()[1]->text == u"Save");
    CHECK(f.box.text().rect().y1 == 170 - style().text_gap);
}

TEST_CASE("clicking an answer gives it once and closes the box")
{
    Fixture f;
    f.desktop.update(at(200, 180, true));
    f.desktop.update(at(200, 180));
    CHECK(f.box.take_answer() == 1u);
    CHECK_FALSE(f.box.take_answer());
    f.desktop.update(at(0, 0));
    CHECK(f.desktop.root().children().size() == 1);
}

TEST_CASE("enter answers with the first button, escape and the close button with the last")
{
    SUBCASE("enter")
    {
        Fixture f;
        f.keys({ Key::Enter });
        CHECK(f.box.take_answer() == 0u);
    }
    SUBCASE("escape")
    {
        Fixture f;
        f.keys({ Key::Escape });
        CHECK(f.box.take_answer() == 2u);
    }
    SUBCASE("close button")
    {
        Fixture f;
        const Rect r = f.box.find("close")->rect();
        f.desktop.update(at(r.x0 + 1, r.y0 + 1, true));
        f.desktop.update(at(r.x0 + 1, r.y0 + 1));
        CHECK_FALSE(f.box.closing()); // Not before the answer is read
        CHECK(f.box.take_answer() == 2u);
        CHECK(f.box.closing());
    }
}
