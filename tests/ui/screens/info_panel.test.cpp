// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/info_panel.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"

#include <algorithm>
#include <cmath>
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
        return { { "A", u"a", 0, { }, false, { } }, { "B", u"b", 3, { }, false, { } }, { "C", u"c", 6, { }, false, { } } };
    }
} // namespace

TEST_CASE("tabs go round the ring from up and to the right, the fourth mirroring the first")
{
    const Point first = info_tab_offset(0);
    CHECK(first.x > 0);
    CHECK(first.y < 0);
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
    CHECK_FALSE(f.panel->covers({ disc.x1 - 2, disc.y0 + 2 }));
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
    CHECK(strip.y0 == f.panel->disc().y0);
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
    CHECK(one.x > 0);
    CHECK(one.y < 0);
    const Point left = info_choice_offset(0, 0, 3);
    const Point middle = info_choice_offset(0, 1, 3);
    const Point right = info_choice_offset(0, 2, 3);
    CHECK(middle.x == one.x);
    CHECK(middle.y == one.y);
    CHECK(std::hypot(left.x - one.x, left.y - one.y) == doctest::Approx(std::hypot(right.x - one.x, right.y - one.y)).epsilon(0.02)); // Whole units
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
    auto second = f.panel->body().find("section1");
    auto first = f.panel->body().find("section0");
    REQUIRE(first);
    REQUIRE(second);
    CHECK(second->rect().width() > first->rect().width());
    f.click(Fixture::middle(first->rect()));
    CHECK(f.panel->take_section() == 0u);

    // A single section needs no buttons.
    page.sections = { u"Only" };
    f.panel->show_page(page);
    CHECK(!f.panel->body().find("section0"));
}

TEST_CASE("a page's graph is plotted with each series named under it")
{
    Fixture f;
    f.panel->show(three(), 0);
    InfoPage page;
    page.graph = { { u"Up", { 0, 1, 2 } }, { u"Down", { 2, 1, 0 } } };
    f.panel->show_page(page);
    CHECK(f.panel->body().find("graph"));
    CHECK(f.panel->body().find("series1"));
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

TEST_CASE("a paint tab opens colours above itself for the pot pressed, and tells the colour picked")
{
    Fixture f;
    std::vector<InfoTab> tabs = three();
    tabs[1].paints = { 4, 9 };
    f.panel->show(std::move(tabs), 0);
    f.desktop.update({ });
    CHECK_FALSE(f.panel->tab(1).toggle);
    CHECK_FALSE(f.panel->painting());

    f.click(Fixture::middle(f.panel->tab(1).rect()));
    REQUIRE(f.panel->painting() == 1);
    REQUIRE(f.panel->palette());
    ColourPalette& palette = *f.panel->palette();
    CHECK(palette.count() == game_skins::info_panel_style().palette_count);
    CHECK(palette.rect().y1 < f.panel->tab(1).rect().y0);
    CHECK(palette.rect().x1 <= 1000);
    const std::size_t pot = palette.current() == 4 ? 0 : 1;
    CHECK(palette.current() == (pot == 0 ? 4U : 9U));

    f.click(Fixture::middle(palette.swatch(21).rect()));
    const auto paint = f.panel->take_paint();
    REQUIRE(paint);
    CHECK(paint->tab == 1);
    CHECK(paint->pot == pot);
    CHECK(paint->colour == 21);
    CHECK_FALSE(f.panel->take_paint());
    // Picking puts the colours away.
    CHECK_FALSE(f.panel->painting());
    CHECK(!f.panel->palette());
}

TEST_CASE("the colours go away when their tab is pressed again, another tab is, or the owner asks")
{
    Fixture f;
    std::vector<InfoTab> tabs = three();
    tabs[1].paints = { 4 };
    f.panel->show(std::move(tabs), 0);
    f.desktop.update({ });
    const Point paint = Fixture::middle(f.panel->tab(1).rect());
    f.click(paint);
    CHECK(f.panel->painting());
    f.click(paint);
    CHECK_FALSE(f.panel->painting());
    f.click(paint);
    f.click(Fixture::middle(f.panel->tab(2).rect()));
    CHECK_FALSE(f.panel->painting());
    f.click(paint);
    f.panel->close_palette();
    CHECK_FALSE(f.panel->painting());
    CHECK_FALSE(f.panel->take_paint());
}

TEST_CASE("the frame is drawn either from the skins or as shapes in code")
{
    Fixture f;
    f.panel->show(three(), 0);
    f.panel->set_body_size(info_panel::kPageWidth, 40);
    f.desktop.update({ });
    const SkinLibrary skins;
    const FontSet fonts;

    SUBCASE("in code, only shapes, all within the strip and the disc and their shadows")
    {
        InfoPanelStyle style = game_skins::info_panel_style();
        style.look = PanelLook::Coded;
        InfoPanel& coded = f.desktop.root().add<InfoPanel>("coded", style);
        coded.show(three(), 0);
        coded.set_body_size(info_panel::kPageWidth, 40);
        f.desktop.update({ });
        DrawList list;
        DrawContext context { list, skins, fonts };
        coded.draw(context);
        CHECK(list.vertices().empty());
        REQUIRE_FALSE(list.shape_vertices().empty());
        const Rect s = coded.strip();
        const Rect d = coded.disc();
        // One pixel of smoothing past the shadows.
        const float reach = info_panel::kShadowOffset + 1;
        for (const ShapeVertex& v : list.shape_vertices())
        {
            CHECK(v.x >= s.x0 - 1);
            CHECK(v.x <= d.x1 + reach);
            CHECK(v.y >= std::min(s.y0, d.y0) - 1);
            CHECK(v.y <= std::max(s.y1, d.y1) + reach);
        }
    }
    SUBCASE("from the skins, no shapes")
    {
        InfoPanelStyle style = game_skins::info_panel_style();
        style.look = PanelLook::Texture;
        InfoPanel& textured = f.desktop.root().add<InfoPanel>("textured", style);
        textured.show(three(), 0);
        f.desktop.update({ });
        DrawList list;
        DrawContext context { list, skins, fonts };
        textured.draw(context);
        CHECK(list.shape_vertices().empty());
    }
}

TEST_CASE("a page's controls tell the changes made to them by their ids")
{
    Fixture f;
    f.panel->show(three(), 0);
    InfoPage page;
    InfoControl tick;
    tick.id = 5;
    tick.kind = InfoControlKind::Tick;
    tick.label = u"Wait";
    InfoControl pots;
    pots.id = 7;
    pots.kind = InfoControlKind::Paint;
    pots.paints = { 3, 4 };
    page.controls = { tick, pots };
    f.panel->show_page(page);
    f.desktop.update({ });
    REQUIRE(f.panel->control(5));
    CHECK(!f.panel->control(6));

    f.click(Fixture::middle(f.panel->control(5)->rect()));
    auto change = f.panel->take_change();
    REQUIRE(change);
    CHECK(change->id == 5);
    CHECK(change->value == 1);
    CHECK_FALSE(f.panel->take_change());

    f.click(Fixture::middle(f.panel->control(7)->rect()));
    REQUIRE(f.panel->palette());
    CHECK(f.panel->palette()->rect().y1 < f.panel->control(7)->rect().y0);
    f.click(Fixture::middle(f.panel->palette()->swatch(21).rect()));
    change = f.panel->take_change();
    REQUIRE(change);
    CHECK(change->id == 7);
    CHECK(change->value == 21);
    CHECK(!f.panel->palette());
}

TEST_CASE("an info widget is named by its string key and part, or by its place without one")
{
    CHECK(keyed_name("Park_Name", "value", 3) == "Park_Name_value");
    CHECK(keyed_name("Park_Name", "", 3) == "Park_Name");
    CHECK(keyed_name("", "value", 3) == "value3");
}

TEST_CASE("turned the other way, the panel stands in the bottom-left corner with its tabs mirrored")
{
    Desktop desktop;
    InfoPanelStyle style = game_skins::info_panel_style();
    style.left = true;
    style.raise = 50;
    auto& panel = desktop.root().add<InfoPanel>("placing", style);
    desktop.layout({ 0, 0, 1000, 700 });
    panel.show(three(), 3);
    desktop.update({ });
    const Rect d = panel.disc();
    CHECK(d.x0 == doctest::Approx(info_panel::kDiscMargin));
    CHECK(d.y1 == doctest::Approx(700 - info_panel::kDiscMargin - 50));
    const Rect tab = desktop.root().find("placing/A")->rect();
    CHECK((tab.x0 + tab.x1) / 2 - (d.x0 + d.x1) / 2 == doctest::Approx(-info_tab_offset(0).x));
    // With no page there is no strip to take the pointer.
    CHECK_FALSE(panel.covers({ d.x1 + 40, (d.y0 + d.y1) / 2 }));
}
