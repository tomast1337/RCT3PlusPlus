// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/keyboard_focus.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    class Focusable : public Widget
    {
    public:
        using Widget::Widget;
        [[nodiscard]] bool takes_focus() const override
        {
            return true;
        }
    };

    struct Tree
    {
        Widget root { "root" };
        Focusable& a = root.add<Focusable>("a");
        Widget& group = root.add<Widget>("group");
        Focusable& b = group.add<Focusable>("b");
        Focusable& c = group.add<Focusable>("c");
        Focusable& d = root.add<Focusable>("d");
    };

    KeyboardState tab(bool shift = false)
    {
        KeyboardState k;
        k.presses.push_back({ Key::Tab, shift });
        return k;
    }
} // namespace

TEST_CASE("the focus order is tree order, wrapping both ways")
{
    Tree t;
    CHECK(next_focus(t.root, nullptr, false) == &t.a);
    CHECK(next_focus(t.root, nullptr, true) == &t.d);
    CHECK(next_focus(t.root, &t.a, false) == &t.b);
    CHECK(next_focus(t.root, &t.c, false) == &t.d);
    CHECK(next_focus(t.root, &t.d, false) == &t.a);
    CHECK(next_focus(t.root, &t.a, true) == &t.d);
    CHECK(next_focus(t.root, &t.b, true) == &t.a);

    SUBCASE("hidden and disabled widgets are skipped, with what is inside them")
    {
        t.group.visible = false;
        CHECK(next_focus(t.root, &t.a, false) == &t.d);
        t.group.visible = true;
        t.d.enabled = false;
        CHECK(next_focus(t.root, &t.c, false) == &t.a);
    }
    SUBCASE("nothing when nothing takes it")
    {
        Widget empty;
        CHECK(next_focus(empty, nullptr, false) == nullptr);
    }
}

TEST_CASE("tab moves the focus within the top-most window and shows it")
{
    Desktop desktop;
    Widget& back = desktop.root().add<Widget>("back");
    back.add<Focusable>("x");
    Widget& top = desktop.root().add<Widget>("top");
    Focusable& first = top.add<Focusable>("first");
    Focusable& second = top.add<Focusable>("second");
    desktop.layout({ 0, 0, 100, 100 });

    desktop.update({ }, tab());
    CHECK(desktop.keyboard_focus() == &first);
    CHECK(desktop.keyboard_focus_shown());
    desktop.update({ }, tab());
    CHECK(desktop.keyboard_focus() == &second);
    desktop.update({ }, tab());
    CHECK(desktop.keyboard_focus() == &first);
    desktop.update({ }, tab(true));
    CHECK(desktop.keyboard_focus() == &second);

    SUBCASE("a press hides the mark")
    {
        desktop.update({ { 50, 50 }, true, true });
        CHECK_FALSE(desktop.keyboard_focus_shown());
    }
    SUBCASE("the mark is drawn around the focused widget")
    {
        second.placement = top_left(10, 10, 20, 20);
        desktop.layout({ 0, 0, 100, 100 });
        DrawList list;
        const SkinLibrary skins;
        const FontSet fonts;
        DrawContext context { list, skins, fonts };
        desktop.draw(context);
        const float o = desktop.focus_mark_style.outset;
        float x0 = 1e9f;
        float x1 = -1e9f;
        for (const Vertex& v : list.vertices())
        {
            x0 = std::min(x0, v.x);
            x1 = std::max(x1, v.x);
        }
        CHECK(x0 == 10 - o);
        CHECK(x1 == 30 + o);
    }
}

TEST_CASE("the mark is cut by clipping ancestors")
{
    Widget root;
    Widget& view = root.add<Widget>("view");
    view.clip_children = true;
    Focusable& f = view.add<Focusable>("f");
    // Not laid out: every rectangle is empty, so the clip hides everything.
    DrawList list;
    draw_focus_mark(list, f, { });
    CHECK(list.vertices().empty());
    CHECK(list.clip() == nullptr);
}
