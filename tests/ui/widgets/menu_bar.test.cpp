// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/menu_bar.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    // Without fonts each title is 60 wide from x 0: File 0-60, Edit 60-120, View 120-180. Menus open
    // below the 20 tall bar, rows 20 tall from y 20.
    MenuBarStyle style()
    {
        MenuBarStyle s;
        s.first_indent = 0;
        s.menu.padding = { };
        s.menu.row_height = 20;
        return s;
    }

    std::vector<MenuItem> rows(std::initializer_list<std::u16string_view> texts)
    {
        std::vector<MenuItem> v;
        for (auto t : texts)
            v.push_back({ .text = std::u16string(t) });
        return v;
    }

    struct Fixture
    {
        Desktop desktop;
        MenuBar& bar = desktop.root().add<MenuBar>("bar", style());

        Fixture()
        {
            bar.placement = top_left(0, 0, 400, 20);
            bar.menus = { { u"File", rows({ u"New", u"Quit" }) }, { u"Edit", rows({ u"Undo" }) }, { u"View", rows({ u"Zoom in", u"Zoom out" }) } };
            desktop.layout({ 0, 0, 400, 400 });
            desktop.update({ { 390, 390 }, false, true });
        }

        void at(float x, float y, bool down)
        {
            desktop.update({ { x, y }, down, true });
        }
    };
} // namespace

TEST_CASE("pressing a title opens its menu, and choosing reports both indices")
{
    Fixture f;
    f.at(70, 10, true);
    f.at(70, 10, false);
    CHECK(f.bar.open_menu() == 1u);
    REQUIRE(f.bar.menu() != nullptr);
    CHECK(f.bar.menu()->rect().x0 == 60);
    CHECK(f.bar.menu()->rect().y0 == 20);

    f.at(70, 30, true);
    f.at(70, 30, false);
    CHECK(f.bar.take_choice() == MenuChoice { 1, 0 });
    CHECK_FALSE(f.bar.open_menu());
}

TEST_CASE("with a menu open, the pointer and the side keys move between menus")
{
    Fixture f;
    f.at(10, 10, true);
    f.at(10, 10, false);
    f.at(130, 10, false);
    CHECK(f.bar.open_menu() == 2u);
    CHECK(f.desktop.root().children().size() == 2);

    KeyboardState keys;
    keys.presses = { { Key::Right } };
    f.desktop.update({ { 390, 390 }, false, true }, keys);
    CHECK(f.bar.open_menu() == 0u); // Round to the first
    f.desktop.update({ { 390, 390 }, false, true });
    CHECK(f.desktop.root().children().size() == 2);

    SUBCASE("pressing the open title closes it")
    {
        f.at(10, 10, true);
        f.at(10, 10, false);
        CHECK_FALSE(f.bar.open_menu());
        CHECK(f.desktop.root().children().size() == 1);
    }
}

TEST_CASE("the keys open the marked menu from the bar")
{
    Fixture f;
    f.desktop.set_keyboard_focus(&f.bar);
    KeyboardState keys;
    keys.presses = { { Key::Left }, { Key::Down } };
    f.desktop.update({ { 390, 390 }, false, true }, keys);
    CHECK(f.bar.open_menu() == 2u);
    keys.presses = { { Key::Down }, { Key::Down }, { Key::Enter } };
    f.desktop.update({ { 390, 390 }, false, true }, keys);
    f.desktop.update({ { 390, 390 }, false, true }, keys);
    CHECK(f.bar.take_choice() == MenuChoice { 2, 1 });
}
