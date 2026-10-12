// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/toolbar.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    ToolbarStyle style()
    {
        ToolbarStyle s;
        s.padding = { 2, 2, 2, 2 };
        s.button_size = 20;
        s.gap = 4;
        s.separator_room = 10;
        return s;
    }
} // namespace

TEST_CASE("toolbar buttons go in a row with separators between")
{
    Desktop desktop;
    Toolbar& bar = desktop.root().add<Toolbar>("tools", style());
    Button& a = bar.add_button("a", "IconA", u"First");
    bar.add_separator();
    Button& b = bar.add_button("b", "IconB");
    bar.placement = top_left(0, 0, bar.length(), 24);
    desktop.layout({ 0, 0, 300, 300 });
    CHECK(bar.length() == 2 + 20 + 4 + 10 + 4 + 20 + 2);
    CHECK(a.rect() == Rect { 2, 2, 22, 22 });
    CHECK(b.rect() == Rect { 40, 2, 60, 22 });
    CHECK(a.tooltip == u"First");

    SUBCASE("hidden buttons close up")
    {
        a.visible = false;
        desktop.update({ { 290, 290 }, false, true });
        desktop.layout({ 0, 0, 300, 300 });
        CHECK(b.rect().x0 == 2 + 10 + 4);
    }
    SUBCASE("a click reaches the button")
    {
        desktop.update({ { 50, 10 }, true, true });
        desktop.update({ { 50, 10 }, false, true });
        CHECK(b.take_click());
    }
}

TEST_CASE("a vertical toolbar stacks its buttons")
{
    Desktop desktop;
    Toolbar& bar = desktop.root().add<Toolbar>("tools", style(), Orientation::Vertical);
    bar.add_button("a", "IconA");
    Button& b = bar.add_button("b", "IconB");
    desktop.layout({ 0, 0, 300, 300 });
    CHECK(b.rect() == Rect { 2, 26, 22, 46 });
}
