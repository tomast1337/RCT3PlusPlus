// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/scroll_area.hpp"

#include "ui/desktop.hpp"
#include "ui/widgets/label.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    ScrollBarStyle style()
    {
        ScrollBarStyle s;
        s.thickness = 10;
        s.wheel_step = 30;
        return s;
    }

    // One glyph, 'x', 10 units wide; lines are 20 tall.
    Font test_font()
    {
        ovl::Font f;
        f.metrics = { 0.1f, 2.0f, 0.5f };
        f.has_code_table = true;
        f.glyphs.push_back({ u'x', 1, 0, { 0, 0, 0.1f, 0.1f }, { 0, 0 }, 0 });
        f.glyphs.push_back({ u' ', 1, 0, { 0, 1, 0, 1 }, { 0, 0 }, 0 });
        const std::array<Font::Page, 1> pages = { { { 2, 100, 100 } } };
        return Font::from(f, pages);
    }
} // namespace

TEST_CASE("a scroll area sizes its bar from the children and moves them")
{
    Desktop desktop;
    ScrollArea& area = desktop.root().add<ScrollArea>("area", style());
    area.placement = top_left(0, 0, 110, 100);
    Widget& tall = area.content().add<Widget>("tall");
    tall.placement = top_left(0, 0, 50, 250);
    desktop.layout({ 0, 0, 300, 300 });

    desktop.update({ { 200, 200 }, false, true });
    CHECK(area.bar().content == 250);
    CHECK(area.bar().visible == 100);
    CHECK(area.content().rect().x1 == 100); // Leaves room for the bar

    SUBCASE("the wheel over a child scrolls the area")
    {
        desktop.update({ { 20, 20 }, false, true, -2 });
        CHECK(area.bar().position == 60);
        CHECK(tall.rect().y0 == -60);
    }
    SUBCASE("shrinking content pulls the view back")
    {
        area.bar().position = 150;
        tall.placement = top_left(0, 0, 50, 120);
        desktop.update({ { 200, 200 }, false, true });
        desktop.update({ { 200, 200 }, false, true });
        CHECK(area.bar().position == 20);
    }
    SUBCASE("children outside the view are cut off")
    {
        DrawList list;
        const SkinLibrary skins;
        const FontSet fonts;
        DrawContext context { list, skins, fonts };
        desktop.draw(context);
        CHECK(list.clip() == nullptr);
        CHECK_FALSE(desktop.hit({ 20, 150 }));
    }
}

TEST_CASE("wrapped labels count their text, not their box")
{
    FontSet fonts;
    fonts.set(FontRole::Small, test_font());
    Desktop desktop;
    ScrollArea& area = desktop.root().add<ScrollArea>("area", style());
    area.placement = top_left(0, 0, 40, 50);
    // Three words of 20 units in a 30 wide box: one per line, 60 tall in a 50 tall view.
    Label& label = area.content().add<Label>("text", u"xx xx xx");
    label.style.wrap = true;
    desktop.layout({ 0, 0, 300, 300 });

    const UpdateContext context { 0, &fonts };
    desktop.update({ { 200, 200 }, false, true }, context);
    CHECK(label.extent().y1 == 60);
    desktop.update({ { 200, 200 }, false, true }, context);
    CHECK(area.bar().content == 60);
    CHECK(area.bar().range() == 10);
}

TEST_CASE("the wheel goes up the tree to the first widget that takes it")
{
    Desktop desktop;
    ScrollArea& outer = desktop.root().add<ScrollArea>("outer", style());
    outer.placement = top_left(0, 0, 110, 100);
    outer.content().add<Widget>("tall").placement = top_left(0, 0, 50, 400);
    ScrollArea& inner = outer.content().add<ScrollArea>("inner", style());
    inner.placement = top_left(0, 0, 60, 50); // Nothing to scroll inside
    desktop.layout({ 0, 0, 300, 300 });
    desktop.update({ { 200, 200 }, false, true });

    desktop.update({ { 20, 20 }, false, true, -1 });
    CHECK(inner.bar().position == 0);
    CHECK(outer.bar().position == 30);
}
