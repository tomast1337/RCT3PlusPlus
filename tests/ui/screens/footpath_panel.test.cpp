// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/footpath_panel.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    struct Fixture
    {
        StringTable strings;
        Desktop desktop;
        FootpathPanel* panel = nullptr;

        Fixture()
        {
            strings.add("Path_Left", u"Turn left");
            panel = &desktop.root().add<FootpathPanel>("paths", game_skins::footpath_panel_style(), strings);
            desktop.layout({ 0, 0, 1000, 700 });
            desktop.update({ });
        }

        void hover(Point p)
        {
            PointerState s;
            s.position = p;
            desktop.update(s);
            desktop.update(s);
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

        void open()
        {
            panel->show({ { "a", u"Gravel", u"$1" }, { "b", u"Bricks", u"$2" }, { "c", u"Sand", u"$3" } }, { { "q", u"Ropes", u"$4" } });
            desktop.update({ });
        }
    };
} // namespace

TEST_CASE("the tabs sit on the ring, the first up and to the left, then clockwise")
{
    const Point first = footpath_tab_offset(footpath_panel::kControlsSlot);
    CHECK(first.x < 0);
    CHECK(first.y < 0);
    const Point second = footpath_tab_offset(footpath_panel::kFollowSlot);
    CHECK(second.x > first.x);
    // A third of a turn on from the first: level with the disc's middle, on its right.
    const Point paths = footpath_tab_offset(footpath_panel::kPathsSlot);
    CHECK(paths.x > 0);
    CHECK(paths.y == 0);
    const Point queues = footpath_tab_offset(footpath_panel::kQueuesSlot);
    CHECK(queues.y > 0);
}

TEST_CASE("the panel sits in the bottom-left corner with its controls out")
{
    Fixture f;
    CHECK_FALSE(f.panel->showing());
    f.open();
    CHECK(f.panel->showing());
    CHECK(f.panel->page() == FootpathPage::Controls);
    const Rect disc = f.panel->disc();
    CHECK(disc.x0 == info_panel::kDiscMargin);
    CHECK(disc.y1 == 700 - info_panel::kDiscMargin);
    const Rect strip = f.panel->strip();
    CHECK(strip.x0 < disc.x1);
    CHECK(strip.x1 > disc.x1);
    CHECK(f.panel->covers(Fixture::middle(disc)));
    CHECK(f.panel->covers({ strip.x1 - 20, strip.y1 - 20 }));
    CHECK_FALSE(f.panel->covers({ 900, 100 }));
    for (std::size_t i = 0; i < kFootpathControls; ++i)
        CHECK(f.panel->control(i).visible);
    CHECK(f.panel->tab(FootpathPage::Controls).on);
}

TEST_CASE("a control pressed is heard once, and one that cannot be used is not")
{
    Fixture f;
    f.open();
    f.click(Fixture::middle(f.panel->control(1).rect()));
    CHECK(f.panel->take_control() == std::optional<std::size_t> { 1 });
    CHECK_FALSE(f.panel->take_control());

    std::array<bool, kFootpathControls> usable { };
    usable.fill(true);
    usable[0] = false;
    f.panel->set_controls(usable, 5, false);
    f.desktop.update({ });
    f.click(Fixture::middle(f.panel->control(0).rect()));
    CHECK_FALSE(f.panel->take_control());
}

TEST_CASE("the line under the controls tells what the one under the pointer does")
{
    Fixture f;
    f.open();
    f.panel->set_status(u"Click somewhere");
    f.hover(Fixture::middle(f.panel->control(0).rect()));
    CHECK(f.panel->text_line().text == u"Turn left");
    CHECK(f.panel->status_line().text == u"Click somewhere");
    CHECK(f.panel->status_line().visible);
    f.hover({ 900, 100 });
    CHECK(f.panel->text_line().text.empty());
}

TEST_CASE("a style page lists the styles; choosing one marks it and goes back to the controls")
{
    Fixture f;
    f.open();
    f.click(Fixture::middle(f.panel->tab(FootpathPage::Paths).rect()));
    CHECK(f.panel->page() == FootpathPage::Paths);
    CHECK(f.panel->style_count() == 3);
    CHECK_FALSE(f.panel->control(0).visible);
    CHECK_FALSE(f.panel->status_line().visible);
    f.click(Fixture::middle(f.panel->style_button(2).rect()));
    const auto picked = f.panel->take_style();
    REQUIRE(picked);
    CHECK_FALSE(picked->queue);
    CHECK(picked->index == 2);
    CHECK(f.panel->page() == FootpathPage::Controls);
    REQUIRE(f.panel->chosen());
    CHECK(f.panel->chosen()->index == 2);

    f.click(Fixture::middle(f.panel->tab(FootpathPage::Queues).rect()));
    CHECK(f.panel->page() == FootpathPage::Queues);
    CHECK(f.panel->style_count() == 1);
    f.click(Fixture::middle(f.panel->style_button(0).rect()));
    const auto queue = f.panel->take_style();
    REQUIRE(queue);
    CHECK(queue->queue);
}

TEST_CASE("the open page's tab folds the strip away, and the close button hides the panel")
{
    Fixture f;
    f.open();
    f.click(Fixture::middle(f.panel->tab(FootpathPage::Controls).rect()));
    CHECK(f.panel->page() == FootpathPage::None);
    f.click(Fixture::middle(f.panel->tab(FootpathPage::Controls).rect()));
    CHECK(f.panel->page() == FootpathPage::Controls);

    CHECK(f.panel->following()); // The camera follows from the start
    f.click(Fixture::middle(f.panel->follow_tab().rect()));
    CHECK_FALSE(f.panel->following());
    CHECK_FALSE(f.panel->routing());
    f.click(Fixture::middle(f.panel->route_tab().rect()));
    CHECK(f.panel->routing());
    CHECK_FALSE(f.panel->following());
    // Removing and routing turn each other off.
    f.click(Fixture::middle(f.panel->remove_tab().rect()));
    CHECK(f.panel->removing());
    CHECK_FALSE(f.panel->routing());
    f.click(Fixture::middle(f.panel->route_tab().rect()));
    CHECK(f.panel->routing());
    CHECK_FALSE(f.panel->removing());

    f.click({ footpath_panel::kCloseLeft + info_panel::kCloseSize / 2, 700 - footpath_panel::kCloseBottom - info_panel::kCloseSize / 2 });
    CHECK_FALSE(f.panel->showing());
    CHECK(f.panel->take_closed());
    CHECK_FALSE(f.panel->take_closed());
}
