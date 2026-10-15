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
        return { { "A", u"a", 0, { } }, { "B", u"b", 3, { } }, { "C", u"c", 6, { } } };
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

TEST_CASE("the strip fits round the page's width beside the disc, and stands full height behind it")
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
    CHECK(strip.height() == info_panel::kStripHeight);
    // A new page replaces the old one; an empty one has no strip.
    f.panel->show_page({ });
    CHECK(f.panel->body().children().empty());
    f.desktop.update({ });
    CHECK(f.panel->body().rect().width() == 0);
}

TEST_CASE("an action tab is reported when pressed but never opens, and the panel may open with no tab open")
{
    Fixture f;
    std::vector<InfoTab> tabs = three();
    tabs[0].action = true;
    f.panel->show(std::move(tabs), 3);
    f.desktop.update({ });
    CHECK_FALSE(f.panel->tab(1).on);
    const Rect r = f.panel->tab(0).rect();
    const Point p { (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2 };
    f.desktop.update({ p, true });
    f.desktop.update({ p, false });
    CHECK(f.panel->take_action() == 0u);
    CHECK_FALSE(f.panel->take_action());
    CHECK_FALSE(f.panel->take_picked());
    CHECK_FALSE(f.panel->tab(0).on);
}

TEST_CASE("a tab with choices fans them out round itself and tells the one picked")
{
    Fixture f;
    std::vector<InfoTab> tabs = three();
    tabs[1].choices = { { "Go", u"go", true }, { "Stop", u"stop", false } };
    f.panel->show(std::move(tabs), 0);
    f.desktop.update({ });
    CHECK_FALSE(f.panel->choosing());
    f.click(Fixture::middle(f.panel->tab(1).rect()));
    REQUIRE(f.panel->choosing() == 1u);
    // It is not opened as a page.
    CHECK_FALSE(f.panel->take_picked());
    CHECK(f.panel->picked() == 0);
    CHECK_FALSE(f.panel->choice(1).enabled);
    const Rect go = f.panel->choice(0).rect();
    CHECK(go.width() == info_panel::kChoiceSize);
    f.click(Fixture::middle(go));
    const auto made = f.panel->take_choice();
    REQUIRE(made);
    CHECK(made->first == 1);
    CHECK(made->second == 0);
    CHECK_FALSE(f.panel->choosing());
}

TEST_CASE("choices spread evenly either side of the line out through their tab")
{
    const Point one = info_choice_offset(0, 0, 1);
    CHECK(one.x == -one.y);
    const Point left = info_choice_offset(0, 0, 3);
    const Point middle = info_choice_offset(0, 1, 3);
    const Point right = info_choice_offset(0, 2, 3);
    CHECK(middle.x == one.x);
    CHECK(middle.y == one.y);
    CHECK(left.x == -right.y);
    CHECK(left.y == -right.x);
}

TEST_CASE("a page's sections are buttons that ask for another section")
{
    Fixture f;
    f.panel->show(three(), 0);
    InfoPage page;
    page.sections = { u"First", u"Second part" };
    page.section = 1;
    f.panel->show_page(page);
    f.desktop.update({ });
    const Widget* second = f.panel->body().find("section1");
    const Widget* first = f.panel->body().find("section0");
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(second->rect().width() > first->rect().width());
    f.click(Fixture::middle(first->rect()));
    CHECK(f.panel->take_section() == 0u);

    // A single section needs no buttons.
    page.sections = { u"Only" };
    f.panel->show_page(page);
    CHECK(f.panel->body().find("section0") == nullptr);
}

TEST_CASE("a page's graph is plotted with each series named under it")
{
    Fixture f;
    f.panel->show(three(), 0);
    InfoPage page;
    page.graph = { { u"Up", { 0, 1, 2 } }, { u"Down", { 2, 1, 0 } } };
    f.panel->show_page(page);
    CHECK(f.panel->body().find("graph") != nullptr);
    CHECK(f.panel->body().find("series1") != nullptr);
}

TEST_CASE("the round view sits in the middle of the disc")
{
    Fixture f;
    f.panel->show(three(), 0);
    f.desktop.update({ });
    const Rect d = f.panel->disc();
    const Rect v = f.panel->view();
    CHECK(v.width() == info_panel::kView);
    CHECK(v.height() == info_panel::kView);
    CHECK(v.x0 + v.x1 == doctest::Approx(d.x0 + d.x1));
    CHECK(v.y0 + v.y1 == doctest::Approx(d.y0 + d.y1));
}
