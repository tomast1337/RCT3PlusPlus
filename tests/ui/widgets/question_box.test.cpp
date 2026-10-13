// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/question_box.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    // In a 400 by 300 view, the content inset by 10 all round. With no fonts the text takes no room: the
    // buttons, 8 apart, sit 8 in from the content's bottom-right corner, and the content is 46 high. The row
    // (146) and the hover name (128) with a gap make the content 8 + 282 + 8 wide, so the box is 318 by 66,
    // from (41, 117).
    QuestionBoxStyle style()
    {
        QuestionBoxStyle s;
        s.window.content_inset = { 10, 10, 10, 10 };
        s.button = "Btn";
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
        QuestionBox& box = desktop.root().add<QuestionBox>("box", style(), u"Leave the park?", std::vector<QuestionAnswer> { { u"Yes", "Tick" }, { u"Save", "" }, { u"No", "Cross" } });

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

TEST_CASE("a question box is modal, its answers in the bottom-right corner")
{
    Fixture f;
    CHECK(f.box.modal);
    CHECK(f.box.text().text == u"Leave the park?");
    CHECK(f.box.rect() == Rect { 41, 117, 359, 183 });
    REQUIRE(f.box.buttons().size() == 3);
    // Content from (51, 127) to (349, 173).
    CHECK(f.box.buttons()[2]->rect() == Rect { 311, 135, 341, 165 });
    CHECK(f.box.buttons()[1]->rect() == Rect { 233, 135, 303, 165 }); // No icon: wider, with its name
    CHECK(f.box.buttons()[1]->text == u"Save");
    CHECK(f.box.buttons()[0]->rect() == Rect { 195, 135, 225, 165 });
    CHECK(f.box.buttons()[0]->icon == "Tick");
    CHECK(f.box.buttons()[0]->text.empty());
}

TEST_CASE("the answer under the pointer shows its name")
{
    Fixture f;
    f.desktop.update(at(210, 150));
    f.desktop.update(at(210, 150));
    CHECK(f.box.hover().text == u"Yes");
    f.desktop.update(at(260, 150));
    f.desktop.update(at(260, 150));
    CHECK(f.box.hover().text.empty()); // Its name is on it
}

TEST_CASE("clicking an answer gives it once and closes the box")
{
    Fixture f;
    f.desktop.update(at(250, 150, true));
    f.desktop.update(at(260, 150));
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
