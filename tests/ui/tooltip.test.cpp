// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/tooltip.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    class Target : public Widget
    {
    public:
        using Widget::Widget;
        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
    };

    // Every letter and the space advance 10 and draw a 10 x 10 box; line height 20.
    Font test_font()
    {
        ovl::Font f;
        f.metrics = { 0.1f, 2.0f, 0.5f };
        f.has_code_table = true;
        for (char16_t c = u'a'; c <= u'z'; ++c)
            f.glyphs.push_back({ c, 1, 0, { 0, 0, 0.1f, 0.1f }, { 0, 0 }, 0 });
        f.glyphs.push_back({ u' ', 1, 0, { 0, 1, 0, 1 }, { 0, 0 }, 0 });
        const std::array<Font::Page, 1> pages = { { { 4, 100, 100 } } };
        return Font::from(f, pages);
    }

    TooltipStyle style()
    {
        TooltipStyle s;
        s.skin = "Tip";
        s.delay = 0.5f;
        s.padding = { 2, 3, 4, 5 };
        s.max_width = 50;
        s.offset = { 0, 10 };
        return s;
    }
} // namespace

TEST_CASE("the text comes from the widget or its nearest ancestor")
{
    Widget outer("outer");
    outer.tooltip = u"outer";
    Widget& middle = outer.add<Widget>("middle");
    Widget& inner = middle.add<Widget>("inner");
    CHECK(tooltip_text(&inner) == u"outer");
    middle.tooltip = u"middle";
    CHECK(tooltip_text(&inner) == u"middle");
    inner.tooltip = u"inner";
    CHECK(tooltip_text(&inner) == u"inner");
    CHECK(tooltip_text(nullptr).empty());
}

TEST_CASE("a tooltip shows after the pointer rests")
{
    Widget w("w");
    w.tooltip = u"help";
    Tooltip t;
    const TooltipStyle s = style();

    t.update(&w, { 10, 10 }, false, 0.3f, s); // Arriving is a move: the wait starts now
    t.update(&w, { 10, 10 }, false, 0.3f, s);
    CHECK_FALSE(t.shown());
    SUBCASE("moving starts the wait again")
    {
        t.update(&w, { 15, 10 }, false, 0.3f, s);
        CHECK_FALSE(t.shown());
        t.update(&w, { 15, 10 }, false, 0.3f, s);
        CHECK_FALSE(t.shown());
        t.update(&w, { 15, 10 }, false, 0.3f, s);
        REQUIRE(t.shown());
        CHECK(t.text() == u"help");
        CHECK(t.anchor().x == 15);
    }
    SUBCASE("it stays while the pointer moves over the same text")
    {
        t.update(&w, { 10, 10 }, false, 0.3f, s);
        REQUIRE(t.shown());
        t.update(&w, { 30, 12 }, false, 0.1f, s);
        CHECK(t.shown());
        CHECK(t.anchor().x == 10);
    }
    SUBCASE("other text or none hides it")
    {
        t.update(&w, { 10, 10 }, false, 0.3f, s);
        REQUIRE(t.shown());
        Widget other("other");
        other.tooltip = u"other";
        t.update(&other, { 10, 10 }, false, 0.1f, s);
        CHECK_FALSE(t.shown());
        t.update(nullptr, { 10, 10 }, false, 1, s);
        CHECK_FALSE(t.shown());
    }
    SUBCASE("a press hides it until the text under the pointer changes")
    {
        t.update(&w, { 10, 10 }, false, 0.3f, s);
        REQUIRE(t.shown());
        t.update(&w, { 10, 10 }, true, 0.1f, s);
        CHECK_FALSE(t.shown());
        t.update(&w, { 10, 10 }, false, 2, s);
        CHECK_FALSE(t.shown());
        t.update(nullptr, { 10, 10 }, false, 0.1f, s);
        t.update(&w, { 10, 10 }, false, 0.6f, s);
        CHECK(t.shown());
    }
}

TEST_CASE("the box fits the wrapped text and stays in the view")
{
    FontSet fonts;
    fonts.set(FontRole::Small, test_font());
    Widget w("w");
    w.tooltip = u"abc defg";
    Tooltip t;
    t.update(&w, { 20, 30 }, false, 0, style());
    t.update(&w, { 20, 30 }, false, 1, style());
    REQUIRE(t.shown());

    // "abc defg" is 80 wide: it wraps to "abc" and "defg" (40) under a 50 limit.
    const Rect r = t.box(style(), fonts, { 0, 0, 200, 200 });
    CHECK(r == Rect { 20, 40, 20 + 40 + 6, 40 + 40 + 8 });
    const Rect corner = t.box(style(), fonts, { 0, 0, 50, 60 });
    CHECK(corner.x1 == 50);
    CHECK(corner.y1 == 60);
    CHECK(t.box(style(), FontSet { }, { 0, 0, 200, 200 }).empty());
}

TEST_CASE("the desktop draws the tooltip over the tree")
{
    FontSet fonts;
    fonts.set(FontRole::Small, test_font());
    SkinLibrary skins;
    SkinRef tip;
    tip.layout.xs = { 0, 8 };
    tip.layout.ys = { 0, 8 };
    tip.layout.x_count = 2;
    tip.layout.y_count = 2;
    tip.texture = 7;
    tip.texture_width = 16;
    tip.texture_height = 16;
    skins.add("Tip", tip);

    Desktop desktop;
    desktop.tooltip_style = style();
    Target& target = desktop.root().add<Target>("t");
    target.placement = top_left(0, 0, 50, 50);
    target.tooltip = u"ab";
    desktop.layout({ 0, 0, 200, 200 });

    desktop.update({ { 10, 10 }, false, true }, 0.2f);
    desktop.update({ { 10, 10 }, false, true }, 0.2f);
    CHECK_FALSE(desktop.tooltip().shown());
    desktop.update({ { 10, 10 }, false, true }, 0.4f);
    REQUIRE(desktop.tooltip().shown());

    DrawList list;
    DrawContext context { list, skins, fonts };
    desktop.draw(context);
    REQUIRE(list.batches().size() == 2);
    CHECK(list.batches()[0].texture == 7);
    CHECK(list.batches()[1].texture == 4);

    // The pointer leaving the widget takes the tooltip away.
    desktop.update({ { 100, 100 }, false, true }, 0.1f);
    CHECK_FALSE(desktop.tooltip().shown());
}
