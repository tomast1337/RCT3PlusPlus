// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/dropdown.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    // The field from (10, 10) to (110, 30); its menu opens below, rows 20 tall from y 30.
    DropdownStyle style()
    {
        DropdownStyle s;
        s.menu.padding = { };
        s.menu.row_height = 20;
        return s;
    }

    struct Fixture
    {
        Desktop desktop;
        Dropdown& list = desktop.root().add<Dropdown>("detail", style());

        Fixture()
        {
            list.placement = top_left(10, 10, 100, 20);
            list.items = { u"Low", u"Medium", u"High" };
            list.select(1);
            desktop.layout({ 0, 0, 400, 400 });
            desktop.update({ { 390, 390 }, false, true });
        }

        void at(float x, float y, bool down)
        {
            desktop.update({ { x, y }, down, true });
        }
    };
} // namespace

TEST_CASE("pressing a drop-down opens its menu below it")
{
    Fixture f;
    f.at(50, 20, true);
    REQUIRE(f.list.menu() != nullptr);
    CHECK(f.list.menu()->rect().y0 == 30);
    CHECK(f.list.menu()->rect().x0 == 10);
    CHECK(f.list.menu()->rect().width() >= 100);
    CHECK(f.list.menu()->highlighted() == 1u);
    CHECK(f.list.menu()->items[1].checked);
    f.at(50, 20, false);
    REQUIRE(f.list.menu() != nullptr);

    SUBCASE("a press on a row chooses it")
    {
        f.at(50, 75, true);
        f.at(50, 75, false);
        CHECK(f.list.menu() == nullptr);
        CHECK(f.list.selected() == 2u);
        CHECK(f.list.take_change());
        CHECK(f.desktop.keyboard_focus() == &f.list); // Back from the menu
    }
    SUBCASE("a press on the field closes it")
    {
        f.at(50, 20, true);
        f.at(50, 20, false);
        CHECK(f.list.menu() == nullptr);
        CHECK(f.desktop.root().children().size() == 1);
        CHECK(f.list.selected() == 1u);
    }
}

TEST_CASE("dragging from the field into the menu and releasing chooses")
{
    Fixture f;
    f.at(50, 20, true);
    f.at(50, 35, true);
    CHECK(f.list.menu()->highlighted() == 0u);
    f.at(50, 35, false);
    f.at(50, 35, false);
    CHECK(f.list.menu() == nullptr);
    CHECK(f.list.selected() == 0u);
}

TEST_CASE("the keys step a drop-down without opening it, enter opens it")
{
    Fixture f;
    f.desktop.set_keyboard_focus(&f.list);
    KeyboardState keys;
    keys.presses = { { Key::Down }, { Key::Down } };
    f.desktop.update({ { 390, 390 }, false, true }, keys);
    CHECK(f.list.selected() == 2u);
    CHECK(f.list.menu() == nullptr);
    keys.presses = { { Key::Home }, { Key::Enter } };
    f.desktop.update({ { 390, 390 }, false, true }, keys);
    CHECK(f.list.selected() == 0u);
    REQUIRE(f.list.menu() != nullptr);
    f.desktop.update({ { 390, 390 }, false, true });
    CHECK(f.desktop.keyboard_focus() == f.list.menu());
    keys.presses = { { Key::Down }, { Key::Enter } };
    f.desktop.update({ { 390, 390 }, false, true }, keys);
    f.desktop.update({ { 390, 390 }, false, true });
    CHECK(f.list.selected() == 1u);
    CHECK(f.desktop.keyboard_focus() == &f.list);
}

TEST_CASE("removing a drop-down closes its menu")
{
    Fixture f;
    f.at(50, 20, true);
    f.at(50, 20, false);
    REQUIRE(f.desktop.root().children().size() == 2);
    f.list.close();
    f.desktop.update({ { 390, 390 }, false, true });
    f.desktop.update({ { 390, 390 }, false, true });
    CHECK(f.desktop.root().children().empty());
}
