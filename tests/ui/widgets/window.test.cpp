// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/window.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    PointerState at(float x, float y, bool down = false)
    {
        return { { x, y }, down, true };
    }

    WindowStyle style()
    {
        WindowStyle s;
        s.frame = "Frame";
        s.frame_unfocused = "FrameDim";
        s.background = "Back";
        s.title_button = "TitleButton";
        s.close_icon = "Close";
        return s;
    }

    constexpr Rect kView { 0, 0, 400, 300 };
} // namespace

TEST_CASE("a window starts centred with its content inset")
{
    Desktop desktop;
    Window& w = desktop.root().add<Window>("w", style(), 200, 100);
    desktop.layout(kView);
    CHECK(w.rect() == Rect { 100, 100, 300, 200 });
    const Rect& in = style().content_inset;
    CHECK(w.content().rect() == Rect { 100 + in.x0, 100 + in.y0, 300 - in.x1, 200 - in.y1 });
    CHECK(w.find("close") != nullptr);
}

TEST_CASE("dragging the title bar moves the window")
{
    Desktop desktop;
    Window& w = desktop.root().add<Window>("w", style(), 200, 100);
    desktop.layout(kView);

    desktop.update(at(150, 105, true));
    CHECK(w.dragging());
    desktop.update(at(170, 125, true));
    CHECK(w.rect() == Rect { 120, 120, 320, 220 });
    desktop.update(at(170, 125, false));
    CHECK_FALSE(w.dragging());

    SUBCASE("not below the title bar")
    {
        desktop.update(at(150, 180, true));
        desktop.update(at(190, 180, true));
        CHECK(w.rect() == Rect { 120, 120, 320, 220 });
    }
    SUBCASE("not off the desktop")
    {
        desktop.update(at(150, 125, true));
        desktop.update(at(1000, 125, true));
        CHECK(w.rect().x1 == 400);
    }
    SUBCASE("it keeps its place relative to the centre when the desktop changes")
    {
        desktop.layout({ 0, 0, 250, 200 });
        CHECK(w.rect() == Rect { 45, 70, 245, 170 });
    }
    SUBCASE("it comes back when the desktop shrinks under it")
    {
        desktop.layout({ 0, 0, 150, 150 });
        CHECK(w.rect() == Rect { 0, 45, 200, 145 }); // Wider than the desktop: left edge wins
    }
}

TEST_CASE("the close button closes the window")
{
    Desktop desktop;
    desktop.root().add<Window>("w", style(), 200, 100);
    desktop.layout(kView);
    const Rect close = desktop.root().find("w/close")->rect();
    const Point p { close.x0 + 2, close.y0 + 2 };

    desktop.update({ p, true, true });
    CHECK(desktop.root().children().size() == 1);
    desktop.update({ p, false, true });
    CHECK(desktop.root().children().empty());
}

TEST_CASE("only the top window draws the focused frame")
{
    Desktop desktop;
    Window& a = desktop.root().add<Window>("a", style(), 100, 100);
    Window& b = desktop.root().add<Window>("b", style(), 100, 100);
    desktop.layout(kView);
    desktop.update(at(0, 0));
    CHECK(b.focused());
    CHECK_FALSE(a.focused());
}

TEST_CASE("pressing a window behind raises it")
{
    Desktop desktop;
    Window& a = desktop.root().add<Window>("a", style(), 100, 100);
    Window& b = desktop.root().add<Window>("b", style(), 100, 100);
    b.placement = centred(100, 100, 30, 30);
    desktop.layout(kView);
    desktop.update(at(a.rect().x0 + 5, a.rect().y0 + 5, true));
    desktop.update(at(a.rect().x0 + 5, a.rect().y0 + 5, false));
    CHECK(desktop.root().children().back().get() == &a);
    CHECK(a.focused());
    CHECK_FALSE(b.focused());
}

TEST_CASE("the title is drawn in the title font, clipped to the title box")
{
    rct3::ovl::Font f;
    f.metrics = { 0.1f, 1.0f, 0.2f };
    f.has_code_table = true;
    f.glyphs.push_back({ u'x', 1, 0, { 0, 0, 0.1f, 0.1f }, { 0, 0 }, 0 });
    const std::array<Font::Page, 1> pages = { { { 9, 100, 100 } } };
    FontSet fonts;
    fonts.set(style().title_font, Font::from(f, pages));

    Desktop desktop;
    Window& w = desktop.root().add<Window>("w", style(), 200, 100);
    w.title = u"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"; // Longer than the box
    desktop.layout(kView);

    DrawList list;
    const SkinLibrary skins;
    DrawContext context { list, skins, fonts };
    desktop.draw(context);
    REQUIRE(list.batches().size() == 1);
    CHECK(list.batches()[0].texture == 9);
    const float right = w.rect().x1 - style().title_box.x1;
    for (const Vertex& v : list.vertices())
        CHECK(v.x <= right);
    CHECK(list.clip() == nullptr);
}

TEST_CASE("a resizable window follows the edge or corner it is pressed on")
{
    Desktop desktop;
    Window& w = desktop.root().add<Window>("w", style(), 200, 100);
    w.resizable = true;
    w.min_size = { 150, 80 };
    desktop.layout(kView);
    REQUIRE(w.rect() == Rect { 100, 100, 300, 200 });

    SUBCASE("right edge")
    {
        desktop.update(at(298, 150, true));
        CHECK(w.resizing());
        desktop.update(at(338, 170, true));
        CHECK(w.rect() == Rect { 100, 100, 340, 200 });
        desktop.update(at(338, 170));
        CHECK_FALSE(w.resizing());
    }
    SUBCASE("bottom-right corner, along the edge near the corner")
    {
        desktop.update(at(290, 198, true));
        desktop.update(at(310, 218, true));
        CHECK(w.rect() == Rect { 100, 100, 320, 220 });
    }
    SUBCASE("top-left corner moves the top and left edges")
    {
        desktop.update(at(101, 101, true));
        desktop.update(at(81, 91, true));
        CHECK(w.rect() == Rect { 80, 90, 300, 200 });
        CHECK_FALSE(w.dragging());
    }
    SUBCASE("not smaller than the minimum, and the edge waits for the pointer to come back")
    {
        desktop.update(at(298, 150, true));
        desktop.update(at(150, 150, true));
        CHECK(w.rect().width() == 150);
        desktop.update(at(268, 150, true));
        CHECK(w.rect() == Rect { 100, 100, 270, 200 });
    }
    SUBCASE("not past the desktop")
    {
        desktop.update(at(150, 198, true));
        desktop.update(at(150, 900, true));
        CHECK(w.rect().y1 == kView.y1);
    }
    SUBCASE("the content follows")
    {
        desktop.update(at(298, 150, true));
        desktop.update(at(348, 150, true));
        CHECK(w.content().rect().x1 == 350 - style().content_inset.x1);
    }
    SUBCASE("the title bar still moves it away from the edges")
    {
        desktop.update(at(150, 110, true));
        CHECK(w.dragging());
        CHECK_FALSE(w.resizing());
    }
}

TEST_CASE("a window that is not resizable only moves")
{
    Desktop desktop;
    Window& w = desktop.root().add<Window>("w", style(), 200, 100);
    desktop.layout(kView);
    desktop.update(at(298, 150, true));
    desktop.update(at(338, 150, true));
    CHECK_FALSE(w.resizing());
    CHECK(w.rect() == Rect { 100, 100, 300, 200 });
}

TEST_CASE("the minimum never exceeds the size a window was made at")
{
    Desktop desktop;
    Window& w = desktop.root().add<Window>("w", style(), 100, 60);
    w.resizable = true;
    desktop.layout(kView);
    CHECK(w.min_size.x == 100);
    CHECK(w.min_size.y == 60);
}

TEST_CASE("escape closes the top-most window")
{
    Desktop desktop;
    desktop.root().add<Window>("a", style(), 100, 100);
    desktop.root().add<Window>("b", style(), 100, 100);
    desktop.layout(kView);
    KeyboardState keys;
    keys.presses.push_back({ Key::Escape });
    desktop.update(at(0, 0), keys);
    REQUIRE(desktop.root().children().size() == 1);
    CHECK(desktop.root().children().front()->name() == "a");
}
