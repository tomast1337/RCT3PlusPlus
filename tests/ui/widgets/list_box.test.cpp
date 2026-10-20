// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/list_box.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    ScrollBarStyle bar_style()
    {
        ScrollBarStyle s;
        s.thickness = 10;
        s.wheel_step = 20;
        return s;
    }

    // Rows 20 tall from y 0, 90 wide: four whole rows fit in the 80 tall list.
    ListBoxStyle list_style()
    {
        ListBoxStyle s;
        s.row_height = 20;
        s.padding = { };
        return s;
    }

    struct Fixture
    {
        Desktop desktop;
        ListBox& list = desktop.root().add<ListBox>("list", list_style(), bar_style());

        Fixture()
        {
            list.placement = top_left(0, 0, 100, 80);
            list.items = { u"Bumper cars", u"Carousel", u"Chairlift", u"Drop tower", u"Ferris wheel", u"Ghost train", u"Log flume", u"Monorail", u"Spiral slide", u"Swinging ship" };
            desktop.layout({ 0, 0, 300, 300 });
            desktop.update({ { 200, 200 }, false, true });
        }

        void click(float y, bool release = true)
        {
            desktop.update({ { 20, y }, true, true });
            if (release)
                desktop.update({ { 20, y }, false, true });
        }

        void press(Key key, char32_t character = 0)
        {
            KeyboardState k;
            k.presses.push_back({ key, false, false, character });
            desktop.update({ { 200, 200 }, false, true }, k);
        }
    };
} // namespace

TEST_CASE("pressing a row selects it")
{
    Fixture f;
    CHECK_FALSE(f.list.selected());
    CHECK(f.list.bar().content == 200);
    f.click(45);
    CHECK(f.list.selected() == 2u);
    CHECK(f.list.take_change());
    CHECK_FALSE(f.list.take_change());
    CHECK(rct3::as_pointer(f.desktop.keyboard_focus()) == &f.list);
    CHECK_FALSE(f.list.take_activate());

    SUBCASE("a second press soon after activates it")
    {
        f.click(45);
        CHECK(f.list.take_activate());
        CHECK_FALSE(f.list.take_change());
    }
    SUBCASE("a second press later does not")
    {
        f.desktop.update({ { 200, 200 }, false, true }, { 1.0f });
        f.click(45);
        CHECK_FALSE(f.list.take_activate());
    }
    SUBCASE("a press on another row does not")
    {
        f.click(65);
        CHECK(f.list.selected() == 3u);
        CHECK_FALSE(f.list.take_activate());
    }
    SUBCASE("dragging moves the selection, and below the rows stops at the last in view")
    {
        f.desktop.update({ { 20, 5 }, true, true });
        f.desktop.update({ { 20, 70 }, true, true });
        CHECK(f.list.selected() == 3u);
        f.desktop.update({ { 20, 150 }, true, true });
        CHECK(f.list.selected() == 3u); // The bottom row in view
        CHECK(f.list.bar().position == 0);
    }
}

TEST_CASE("the keys move the selection and keep it in view")
{
    Fixture f;
    f.desktop.set_keyboard_focus(&f.list);
    f.press(Key::Down);
    CHECK(f.list.selected() == 0u);
    f.press(Key::PageDown);
    CHECK(f.list.selected() == 3u); // Four rows in view, less one
    f.press(Key::Down);
    CHECK(f.list.selected() == 4u);
    CHECK(f.list.bar().position == 20); // Row 4 ends at 100, the view is 80
    f.press(Key::End);
    CHECK(f.list.selected() == 9u);
    f.press(Key::Down);
    CHECK(f.list.selected() == 9u);
    f.desktop.update({ { 200, 200 }, false, true });
    CHECK(f.list.bar().position == 120);
    f.press(Key::Home);
    f.desktop.update({ { 200, 200 }, false, true });
    CHECK(f.list.selected() == 0u);
    CHECK(f.list.bar().position == 0);
    f.press(Key::Enter);
    CHECK(f.list.take_activate());
}

TEST_CASE("typing a letter jumps to the next row starting with it")
{
    Fixture f;
    f.desktop.set_keyboard_focus(&f.list);
    f.press(Key::Character, U'c');
    CHECK(f.list.selected() == 1u);
    f.press(Key::Character, U'C');
    CHECK(f.list.selected() == 2u);
    f.press(Key::Character, U'c');
    CHECK(f.list.selected() == 1u); // Round again
    f.press(Key::Character, U'z');
    CHECK(f.list.selected() == 1u);
}

TEST_CASE("the wheel scrolls the list")
{
    Fixture f;
    f.desktop.update({ { 20, 20 }, false, true, { 0, -2 } });
    CHECK(f.list.bar().position == 40);
    f.click(5);
    CHECK(f.list.selected() == 2u);
}

TEST_CASE("an empty list leaves the keys to its parents")
{
    ListBox list("list", list_style(), bar_style());
    CHECK_FALSE(list.on_key({ Key::Down }));
    list.items = { u"one" };
    list.select(4);
    CHECK_FALSE(list.selected());
    list.select(0);
    CHECK(list.selected() == 0u);
    CHECK_FALSE(list.on_key({ Key::Escape }));
}
