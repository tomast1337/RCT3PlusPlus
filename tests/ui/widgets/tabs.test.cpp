// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/tabs.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    TabsStyle style()
    {
        TabsStyle s;
        s.bar_height = 20;
        s.gap = 5;
        s.min_tab_width = 40;
        s.page_gap = 10;
        return s;
    }

    struct Fixture
    {
        Desktop desktop;
        Tabs& tabs = desktop.root().add<Tabs>("tabs", style());
        Widget* pages[3] { };

        Fixture()
        {
            tabs.placement = top_left(0, 0, 200, 150);
            pages[0] = &tabs.add_page("general", u"General");
            pages[1] = &tabs.add_page("sound", u"Sound");
            pages[2] = &tabs.add_page("display", u"Display");
            desktop.layout({ 0, 0, 300, 300 });
            desktop.update({ { 250, 250 }, false, true });
        }

        void key(Key k, bool shift, bool control)
        {
            KeyboardState keys;
            keys.presses.push_back({ k, shift, control });
            desktop.update({ { 250, 250 }, false, true }, keys);
        }
    };
} // namespace

TEST_CASE("only the current page shows, below the tabs")
{
    Fixture f;
    CHECK(f.tabs.count() == 3);
    CHECK(f.tabs.current() == 0);
    CHECK(f.pages[0]->visible);
    CHECK_FALSE(f.pages[1]->visible);
    CHECK(f.pages[0]->rect() == Rect { 0, 30, 200, 150 });
    CHECK(f.tabs.tab(0).on);
    CHECK(f.tabs.tab(1).rect() == Rect { 45, 0, 85, 20 });
}

TEST_CASE("clicking a tab shows its page")
{
    Fixture f;
    f.desktop.update({ { 50, 10 }, true, true });
    f.desktop.update({ { 50, 10 }, false, true });
    CHECK(f.tabs.current() == 1);
    CHECK(f.pages[1]->visible);
    CHECK_FALSE(f.pages[0]->visible);
    CHECK(f.tabs.take_change());

    SUBCASE("clicking the current tab keeps it")
    {
        f.desktop.update({ { 50, 10 }, true, true });
        f.desktop.update({ { 50, 10 }, false, true });
        CHECK(f.tabs.current() == 1);
        CHECK(f.tabs.tab(1).on);
        CHECK_FALSE(f.tabs.take_change());
    }
}

TEST_CASE("control with tab turns the pages")
{
    Fixture f;
    Widget& inside = f.pages[0]->add<Widget>("inside");
    f.desktop.set_keyboard_focus(&inside);
    f.key(Key::Tab, false, true);
    CHECK(f.tabs.current() == 1);
    CHECK(f.tabs.take_change());
    f.key(Key::Tab, true, true);
    CHECK(f.tabs.current() == 0);
    f.key(Key::Tab, true, true);
    CHECK(f.tabs.current() == 2); // Round to the last
    f.key(Key::Tab, false, true);
    CHECK(f.tabs.current() == 0);
}

TEST_CASE("left and right turn the pages only from a tab")
{
    Fixture f;
    Widget& inside = f.pages[0]->add<Widget>("inside");
    f.desktop.set_keyboard_focus(&inside);
    f.key(Key::Right, false, false);
    CHECK(f.tabs.current() == 0);

    f.desktop.set_keyboard_focus(&f.tabs.tab(0));
    f.key(Key::Right, false, false);
    CHECK(f.tabs.current() == 1);
    f.key(Key::Left, false, false);
    f.key(Key::Left, false, false);
    CHECK(f.tabs.current() == 2);
}
