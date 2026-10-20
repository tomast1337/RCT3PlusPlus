// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/menu.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    // Rows 20 tall from y 10 (no padding), separators 6: New 10-30, Open 30-50, line 50-56, Quit 56-76,
    // a disabled Save 76-96.
    MenuStyle style()
    {
        MenuStyle s;
        s.padding = { };
        s.row_height = 20;
        s.separator_height = 6;
        s.min_width = 100;
        return s;
    }

    std::vector<MenuItem> items()
    {
        std::vector<MenuItem> v(5);
        v[0].text = u"New";
        v[1].text = u"Open";
        v[2].separator = true;
        v[3].text = u"Quit";
        v[4].text = u"Save";
        v[4].enabled = false;
        return v;
    }

    struct Fixture
    {
        Desktop desktop;
        Widget& window = desktop.root().add<Widget>("window");
        Menu& menu = desktop.root().add<Menu>("menu", style(), items());

        Fixture()
        {
            window.placement = top_left(0, 0, 300, 300);
            menu.place_at({ 10, 10 });
            desktop.layout({ 0, 0, 400, 400 });
            desktop.update({ { 390, 390 }, false, true });
        }

        void keys(std::initializer_list<Key> list)
        {
            KeyboardState k;
            for (Key key : list)
                k.presses.push_back({ key });
            desktop.update({ { 390, 390 }, false, true }, k);
        }
    };
} // namespace

TEST_CASE("a menu is as tall as its rows and takes the keyboard focus")
{
    Fixture f;
    CHECK(f.menu.rect() == Rect { 10, 10, 110, 96 });
    CHECK(f.menu.row_rect(3) == Rect { 10, 56, 110, 76 });
    CHECK(rct3::as_pointer(f.desktop.keyboard_focus()) == &f.menu);
}

TEST_CASE("the pointer highlights a row and releasing on it chooses")
{
    Fixture f;
    f.desktop.update({ { 50, 35 }, false, true });
    CHECK(f.menu.highlighted() == 1u);
    f.desktop.update({ { 50, 53 }, false, true }); // The separator
    CHECK_FALSE(f.menu.highlighted());
    f.desktop.update({ { 50, 80 }, false, true }); // Disabled
    CHECK_FALSE(f.menu.highlighted());

    f.desktop.update({ { 50, 60 }, true, true });
    CHECK_FALSE(f.menu.closing());
    f.desktop.update({ { 50, 60 }, false, true });
    // Closed in that update: gone from the tree once it is over.
    CHECK(f.desktop.root().children().size() == 1);
}

TEST_CASE("the keys move over enabled rows and choose")
{
    Fixture f;
    f.keys({ Key::Down, Key::Down, Key::Down });
    CHECK(f.menu.highlighted() == 3u); // Past the line
    f.keys({ Key::Down });
    CHECK(f.menu.highlighted() == 3u); // Save is disabled
    f.keys({ Key::Home });
    CHECK(f.menu.highlighted() == 0u);
    f.keys({ Key::Up });
    CHECK(f.menu.highlighted() == 0u);
    f.keys({ Key::Left, Key::Right, Key::Right });
    CHECK(f.menu.take_sideways() == 1);
    CHECK(f.menu.on_key({ Key::Enter }));
    CHECK(f.menu.take_choice() == 0u);
    CHECK(f.menu.closing());
}

TEST_CASE("a press elsewhere or escape closes a menu")
{
    SUBCASE("press")
    {
        Fixture f;
        f.desktop.update({ { 200, 200 }, true, true });
        CHECK(f.desktop.root().children().size() == 1);
    }
    SUBCASE("escape")
    {
        Fixture f;
        f.keys({ Key::Escape });
        CHECK(f.desktop.root().children().size() == 1);
    }
}
