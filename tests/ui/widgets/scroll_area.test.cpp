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
    CHECK(area.vertical()->content == 250);
    CHECK(area.vertical()->visible == 100);
    CHECK(area.content().rect().x1 == 100); // Leaves room for the bar

    SUBCASE("the wheel over a child scrolls the area")
    {
        desktop.update({ { 20, 20 }, false, true, { 0, -2 } });
        CHECK(area.vertical()->position == 60);
        CHECK(tall.rect().y0 == -60);
    }
    SUBCASE("shrinking content pulls the view back")
    {
        area.vertical()->position = 150;
        tall.placement = top_left(0, 0, 50, 120);
        desktop.update({ { 200, 200 }, false, true });
        desktop.update({ { 200, 200 }, false, true });
        CHECK(area.vertical()->position == 20);
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
    CHECK(area.vertical()->content == 60);
    CHECK(area.vertical()->range() == 10);
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

    desktop.update({ { 20, 20 }, false, true, { 0, -1 } });
    CHECK(inner.vertical()->position == 0);
    CHECK(outer.vertical()->position == 30);
}

TEST_CASE("a horizontal bar scrolls content wider than the view")
{
    Desktop desktop;
    ScrollArea& area = desktop.root().add<ScrollArea>("area", std::nullopt, style());
    area.placement = top_left(0, 0, 100, 60);
    REQUIRE(area.vertical() == nullptr);
    REQUIRE(area.horizontal() != nullptr);
    Widget& wide = area.content().add<Widget>("wide");
    wide.placement = top_left(0, 0, 300, 20);
    Widget& stretched = area.content().add<Widget>("stretched");
    stretched.placement = fill();
    desktop.layout({ 0, 0, 300, 300 });

    desktop.update({ { 200, 200 }, false, true });
    CHECK(area.horizontal()->content == 300);
    CHECK(area.horizontal()->visible == 100);
    CHECK(area.horizontal()->rect() == Rect { 0, 50, 100, 60 });
    CHECK(area.content().rect().y1 == 50); // Above the bar
    CHECK(stretched.rect().x1 == 300);     // Follows the content, not the view

    SUBCASE("the usual wheel scrolls sideways when there is no vertical bar")
    {
        desktop.update({ { 20, 20 }, false, true, { 0, -1 } });
        CHECK(area.horizontal()->position == 30);
        CHECK(wide.rect().x0 == -30);
    }
    SUBCASE("so does the sideways wheel")
    {
        desktop.update({ { 20, 20 }, false, true, { -2, 0 } });
        CHECK(area.horizontal()->position == 60);
    }
    SUBCASE("narrower content needs no scrolling")
    {
        wide.placement = top_left(0, 0, 50, 20);
        stretched.visible = false;
        desktop.update({ { 200, 200 }, false, true });
        desktop.update({ { 200, 200 }, false, true });
        CHECK(area.horizontal()->content == 100);
        CHECK(area.horizontal()->range() == 0);
    }
}

TEST_CASE("an area with both bars scrolls each way and leaves the corner free")
{
    Desktop desktop;
    ScrollArea& area = desktop.root().add<ScrollArea>("area", style(), style());
    area.placement = top_left(0, 0, 110, 70);
    area.content().add<Widget>("big").placement = top_left(0, 0, 400, 200);
    desktop.layout({ 0, 0, 300, 300 });
    desktop.update({ { 200, 200 }, false, true });

    CHECK(area.vertical()->rect() == Rect { 100, 0, 110, 60 });
    CHECK(area.horizontal()->rect() == Rect { 0, 60, 100, 70 });
    CHECK(area.vertical()->visible == 60);
    CHECK(area.vertical()->content == 200);
    CHECK(area.horizontal()->visible == 100);
    CHECK(area.horizontal()->content == 400);

    desktop.update({ { 20, 20 }, false, true, { 0, -1 } });
    CHECK(area.vertical()->position == 30);
    CHECK(area.horizontal()->position == 0);
    desktop.update({ { 20, 20 }, false, true, { -1, 0 } });
    CHECK(area.horizontal()->position == 30);
    CHECK(area.content().rect().x0 == -30);
    CHECK(area.content().rect().y0 == -30);
}

TEST_CASE("unwrapped labels count their widest line")
{
    FontSet fonts;
    fonts.set(FontRole::Small, test_font());
    Desktop desktop;
    ScrollArea& area = desktop.root().add<ScrollArea>("area", std::nullopt, style());
    area.placement = top_left(0, 0, 50, 60);
    // Eight glyphs of 10 units on the longest line: 80 wide in a 50 wide view.
    Label& label = area.content().add<Label>("text", u"xx\nxxxx xxx\nx");
    label.placement = fill();
    desktop.layout({ 0, 0, 300, 300 });

    const UpdateContext context { 0, &fonts };
    desktop.update({ { 200, 200 }, false, true }, context);
    CHECK(label.extent().x1 == 80);
    desktop.update({ { 200, 200 }, false, true }, context);
    CHECK(area.horizontal()->content == 80);
    CHECK(area.horizontal()->range() == 30);
}

TEST_CASE("a focused scroll area moves with the keys")
{
    Desktop desktop;
    ScrollArea& area = desktop.root().add<ScrollArea>("area", style(), style());
    area.placement = top_left(0, 0, 110, 110);
    area.content().add<Widget>("big").placement = top_left(0, 0, 400, 500);
    desktop.layout({ 0, 0, 300, 300 });
    desktop.update({ { 20, 20 }, true, true });
    desktop.update({ { 20, 20 }, false, true });
    REQUIRE(desktop.keyboard_focus() == &area);
    ScrollBar& v = *area.vertical();
    ScrollBar& h = *area.horizontal();
    REQUIRE(v.visible == 100);

    const auto press = [&](Key key) {
        KeyboardState k;
        k.presses.push_back({ key });
        desktop.update({ { 200, 200 }, false, true }, k);
    };
    press(Key::Down);
    CHECK(v.position == style().step);
    press(Key::PageDown);
    CHECK(v.position == style().step + (100 - style().step));
    press(Key::End);
    CHECK(v.position == v.range());
    press(Key::Home);
    CHECK(v.position == 0);
    press(Key::Right);
    CHECK(h.position == style().step);
    CHECK(v.position == 0);
    press(Key::Left);
    CHECK(h.position == 0);
}

TEST_CASE("an area with nothing to scroll leaves the keys to its parents")
{
    ScrollArea area("area", style());
    CHECK_FALSE(area.on_key({ Key::Down }));
}
