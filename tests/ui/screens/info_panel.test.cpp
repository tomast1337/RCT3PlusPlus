// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/info_panel.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    struct Fixture
    {
        Desktop desktop;
        InfoPanel* panel = nullptr;

        Fixture()
        {
            panel = &desktop.root().add<InfoPanel>("info", game_skins::info_panel_style());
            desktop.layout({ 0, 0, 1000, 700 });
            desktop.update({ });
        }

        void click(Point p)
        {
            PointerState s;
            s.position = p;
            desktop.update(s);
            s.down = true;
            desktop.update(s);
            s.down = false;
            desktop.update(s);
        }

        static Point middle(const Rect& r)
        {
            return { (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2 };
        }
    };

    std::vector<InfoTab> three()
    {
        return { { "A", u"a", 0 }, { "B", u"b", 3 }, { "C", u"c", 6 } };
    }
} // namespace

TEST_CASE("tabs go round the ring from up and to the right, a quarter turn every three places")
{
    const Point first = info_tab_offset(0);
    CHECK(first.x > 0);
    CHECK(first.y < 0);
    CHECK(first.x == -first.y);
    const Point up = info_tab_offset(1);
    CHECK(up.y < first.y);
    const Point left = info_tab_offset(3);
    CHECK(left.x == -first.x);
    CHECK(left.y == first.y);
}

TEST_CASE("the panel sits in the bottom-right corner, hidden until shown")
{
    Fixture f;
    CHECK_FALSE(f.panel->showing());
    f.panel->show(three(), 1);
    f.desktop.update({ });
    CHECK(f.panel->showing());
    const Rect disc = f.panel->disc();
    CHECK(disc.x1 == 1000 - info_panel::kDiscMargin);
    CHECK(disc.y1 == 700 - info_panel::kDiscMargin);
    CHECK(f.panel->tab_count() == 3);
    CHECK(f.panel->picked() == 1);
    CHECK(f.panel->tab(1).on);
    CHECK_FALSE(f.panel->tab(0).on);
    // The pointer goes through beside the disc, but not on it.
    CHECK(f.panel->covers(Fixture::middle(disc)));
    CHECK_FALSE(f.panel->covers({ disc.x0 + 2, disc.y0 + 2 }));
    CHECK_FALSE(f.panel->covers({ 10, 10 }));
}

TEST_CASE("picking a tab is told once, and the close button hides the panel")
{
    Fixture f;
    f.panel->show(three(), 0);
    f.desktop.update({ });
    f.click(Fixture::middle(f.panel->tab(2).rect()));
    CHECK(f.panel->take_picked() == 2u);
    CHECK_FALSE(f.panel->take_picked());
    CHECK(f.panel->tab(2).on);
    CHECK_FALSE(f.panel->tab(0).on);

    f.click({ 1000 - info_panel::kCloseRight - info_panel::kCloseSize / 2, 700 - info_panel::kCloseBottom - info_panel::kCloseSize / 2 });
    CHECK_FALSE(f.panel->showing());
    CHECK(f.panel->take_closed());
    CHECK_FALSE(f.panel->take_closed());
}

TEST_CASE("the strip fits round the page beside the disc")
{
    Fixture f;
    f.panel->show(three(), 0);
    InfoPage page;
    page.title = u"Title";
    page.lines = { { u"One", u"1" }, { u"Two", u"2" } };
    f.panel->show_page(page);
    f.desktop.update({ });
    const Rect strip = f.panel->strip();
    CHECK(strip.x1 == f.panel->disc().x0 + info_panel::kStripOverlap);
    CHECK(strip.width() == info_panel::kPageWidth + info_panel::kStripExtra);
    CHECK(f.panel->body().children().size() == 5);
    CHECK(strip.height() < info_panel::kStripHeight);
    // A new page replaces the old one.
    f.panel->show_page({ });
    CHECK(f.panel->body().children().empty());
}
