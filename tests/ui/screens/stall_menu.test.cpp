// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/stall_menu.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    StallMenuView three_lines()
    {
        StallMenuView v;
        v.goods = { { .name = u"Burger", .icon = "BurgerIcon" }, { .name = u"Fries", .icon = "FriesIcon" } };
        v.extras = { { .name = u"Ketchup", .icon = "KetchupIcon" }, { .name = u"Salt", .icon = "SaltIcon" } };
        v.lines = { { .name = u"Burger", .icon = "BurgerIcon", .price = 150, .extras = { 0, std::nullopt } }, { .name = u"Big Burger", .icon = "BurgerIcon", .price = 250, .extras = { 0 } }, { .name = u"Fries", .icon = "FriesIcon", .price = 100, .extras = { 1 } } };
        v.most_price = 2000;
        v.price_step = 5;
        return v;
    }

    struct Fixture
    {
        Desktop desktop;
        StallMenu* menu = nullptr;

        explicit Fixture(StallMenuView view)
        {
            menu = &desktop.root().add<StallMenu>("menu", game_skins::info_panel_style(kGamePalette).menu, std::move(view));
            menu->placement = top_left(10, 10, 300, 200);
            desktop.layout({ 0, 0, 1280, 720 });
            desktop.update({ });
            desktop.layout({ 0, 0, 1280, 720 });
            desktop.update({ });
        }

        static Point middle(const Rect& r)
        {
            return { (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2 };
        }

        void click(Point p)
        {
            desktop.update({ p, true });
            desktop.update({ p, false });
        }

        void drag(Point from, Point to)
        {
            desktop.update({ from, true });
            desktop.update({ { (from.x + to.x) / 2, (from.y + to.y) / 2 }, true });
            desktop.update({ to, true });
            desktop.update({ to, false });
        }
    };
} // namespace

TEST_CASE("a band goes over each run of lines that take the same extras")
{
    const std::vector<MenuRow> rows = menu_rows(three_lines());
    REQUIRE(rows.size() == 5);
    CHECK_FALSE(rows[0].line);
    CHECK(rows[0].extras == std::vector<std::size_t> { 0 });
    CHECK(rows[1].line == 0);
    CHECK(rows[2].line == 1);
    CHECK_FALSE(rows[3].line);
    CHECK(rows[3].extras == std::vector<std::size_t> { 1 });
    CHECK(rows[4].line == 2);
    // Lines that take no extras go without a band.
    StallMenuView plain;
    plain.lines.resize(2);
    CHECK(menu_rows(plain).size() == 2);
}

TEST_CASE("a line dropped further down goes in once it is out of its place")
{
    CHECK(drop_place(0, 2) == 1);
    CHECK(drop_place(2, 0) == 0);
    CHECK(drop_place(std::nullopt, 2) == 2);
}

TEST_CASE("goods stack up from the bin, and a click on one puts it at the end of the menu")
{
    Fixture f(three_lines());
    CHECK(f.menu->good_rect(0).y1 == f.menu->bin_rect().y0);
    CHECK(f.menu->good_rect(1).y1 == f.menu->good_rect(0).y0);
    f.click(Fixture::middle(f.menu->good_rect(1)));
    CHECK(f.menu->take_event() == MenuEvent { MenuAction::Add, 1, 3, { } });
}

TEST_CASE("goods dragged onto the menu go in between the lines they drop between")
{
    Fixture f(three_lines());
    const Rect second = f.menu->handle_rect(1);
    f.drag(Fixture::middle(f.menu->good_rect(0)), { second.x1 + 20, second.y0 + 2 });
    CHECK(f.menu->take_event() == MenuEvent { MenuAction::Add, 0, 1, { } });
    // Dropped off the pane, nothing goes on.
    f.drag(Fixture::middle(f.menu->good_rect(0)), { 600, 600 });
    CHECK_FALSE(f.menu->take_event());
}

TEST_CASE("a line dragged by its handle moves, and onto the bin goes off")
{
    Fixture f(three_lines());
    const Rect last = f.menu->handle_rect(2);
    f.drag(Fixture::middle(f.menu->handle_rect(0)), { last.x0 + 4, last.y1 - 2 });
    CHECK(f.menu->take_event() == MenuEvent { MenuAction::Move, 0, 2, { } });
    f.drag(Fixture::middle(f.menu->handle_rect(1)), Fixture::middle(f.menu->bin_rect()));
    CHECK(f.menu->take_event() == MenuEvent { MenuAction::Remove, 1, 0, { } });
}

TEST_CASE("a click on a handle picks the line, and a click on the bin takes it off")
{
    Fixture f(three_lines());
    f.click(Fixture::middle(f.menu->handle_rect(2)));
    CHECK_FALSE(f.menu->take_event());
    CHECK(f.menu->picked() == 2);
    f.click(Fixture::middle(f.menu->bin_rect()));
    CHECK(f.menu->take_event() == MenuEvent { MenuAction::Remove, 2, 0, { } });
    // With nothing picked the bin does nothing.
    f.click(Fixture::middle(f.menu->bin_rect()));
    CHECK_FALSE(f.menu->take_event());
}

TEST_CASE("a tick for an extra reports the line's ticks")
{
    StallMenuView v = three_lines();
    v.lines[0].ticked = 0;
    Fixture f(std::move(v));
    auto tick = f.desktop.root().find("menu/menu/view/content/ItemExtra0_0");
    REQUIRE(tick);
    f.click(Fixture::middle(tick->rect()));
    CHECK(f.menu->take_event() == MenuEvent { MenuAction::Extras, 0, 1, { } });
}
