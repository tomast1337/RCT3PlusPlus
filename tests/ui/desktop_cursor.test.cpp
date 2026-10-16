// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/desktop.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/edit_box.hpp"
#include "ui/widgets/scroll_bar.hpp"
#include "ui/widgets/slider.hpp"
#include "ui/widgets/window.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    PointerState at(float x, float y, bool down = false)
    {
        return { { x, y }, down, true };
    }

    // Takes the pointer and asks for nothing special.
    class Pane : public Widget
    {
    public:
        using Widget::Widget;
        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
    };

    WindowStyle window_style()
    {
        WindowStyle s;
        s.frame = "Frame";
        s.title_button = "TitleButton";
        return s;
    }

    constexpr Rect kView { 0, 0, 400, 300 };
} // namespace

TEST_CASE("the pointer over nothing belongs to the 3D view")
{
    Desktop desktop;
    desktop.root().add<Button>("b", "Button").placement = top_left(0, 0, 50, 20);
    desktop.layout(kView);
    desktop.update(at(200, 200));
    CHECK_FALSE(desktop.cursor());

    SUBCASE("and while a developer tool has it, to nobody")
    {
        desktop.update({ { 10, 10 }, false, false });
        CHECK_FALSE(desktop.cursor());
    }
}

TEST_CASE("a button shows a hand, and not allowed while disabled")
{
    Desktop desktop;
    Button& b = desktop.root().add<Button>("b", "Button");
    b.placement = top_left(0, 0, 50, 20);
    desktop.layout(kView);
    desktop.update(at(10, 10));
    CHECK(desktop.cursor() == CursorKind::Hand);

    b.enabled = false;
    desktop.update(at(10, 10));
    CHECK(desktop.cursor() == CursorKind::NotAllowed);

    SUBCASE("also when its parent is disabled")
    {
        b.enabled = true;
        desktop.root().enabled = false;
        desktop.update(at(10, 10));
        CHECK(desktop.cursor() == CursorKind::NotAllowed);
    }
}

TEST_CASE("something that does not click keeps the arrow, enabled or not")
{
    Desktop desktop;
    Pane& pane = desktop.root().add<Pane>("pane");
    pane.placement = top_left(0, 0, 50, 20);
    desktop.layout(kView);
    desktop.update(at(10, 10));
    CHECK(desktop.cursor() == CursorKind::Arrow);
    pane.enabled = false;
    desktop.update(at(10, 10));
    CHECK(desktop.cursor() == CursorKind::Arrow);
}

TEST_CASE("a text field shows the text beam")
{
    Desktop desktop;
    desktop.root().add<EditBox>("e").placement = top_left(0, 0, 100, 20);
    desktop.layout(kView);
    desktop.update(at(10, 10));
    CHECK(desktop.cursor() == CursorKind::Text);
}

TEST_CASE("a held button keeps its pointer when dragged off it")
{
    Desktop desktop;
    desktop.root().add<Button>("b", "Button").placement = top_left(0, 0, 50, 20);
    desktop.layout(kView);
    desktop.update(at(10, 10, true));
    desktop.update(at(200, 200, true));
    CHECK(desktop.cursor() == CursorKind::Hand);
    desktop.update(at(200, 200));
    CHECK_FALSE(desktop.cursor());
}

TEST_CASE("over what a modal widget shuts out the pointer is the arrow")
{
    Desktop desktop;
    desktop.root().add<Button>("b", "Button").placement = top_left(0, 0, 50, 20);
    Pane& question = desktop.root().add<Pane>("question");
    question.modal = true;
    question.placement = top_left(100, 100, 50, 50);
    desktop.layout(kView);
    desktop.update(at(10, 10));
    CHECK(desktop.cursor() == CursorKind::Arrow);
    desktop.update(at(300, 250));
    CHECK(desktop.cursor() == CursorKind::Arrow);
}

TEST_CASE("a window dragged by its title shows the four-way arrow")
{
    Desktop desktop;
    desktop.root().add<Window>("w", window_style(), 200, 100); // At 100,100 to 300,200
    desktop.layout(kView);
    desktop.update(at(150, 105));
    CHECK(desktop.cursor() == CursorKind::Arrow);
    desktop.update(at(150, 105, true));
    desktop.update(at(170, 125, true));
    CHECK(desktop.cursor() == CursorKind::Move);
    desktop.update(at(170, 125));
    CHECK(desktop.cursor() == CursorKind::Arrow);
}

TEST_CASE("a resizable window shows resize arrows at its edges and corners")
{
    Desktop desktop;
    Window& w = desktop.root().add<Window>("w", window_style(), 200, 100); // At 100,100 to 300,200
    w.resizable = true;
    desktop.layout(kView);
    const auto over = [&](float x, float y) {
        desktop.update(at(x, y));
        return desktop.cursor();
    };
    CHECK(over(102, 150) == CursorKind::ResizeHorizontal);
    CHECK(over(298, 150) == CursorKind::ResizeHorizontal);
    CHECK(over(200, 198) == CursorKind::ResizeVertical);
    CHECK(over(102, 102) == CursorKind::ResizeDiagonalDown);
    CHECK(over(298, 198) == CursorKind::ResizeDiagonalDown);
    CHECK(over(298, 102) == CursorKind::ResizeDiagonalUp);
    CHECK(over(102, 198) == CursorKind::ResizeDiagonalUp);
    CHECK(over(200, 170) == CursorKind::Arrow);

    SUBCASE("and keeps it while the edge follows the pointer")
    {
        desktop.update(at(298, 150, true));
        desktop.update(at(330, 20, true));
        CHECK(w.resizing());
        CHECK(desktop.cursor() == CursorKind::ResizeHorizontal);
    }
}

TEST_CASE("a slider's thumb is taken hold of, the rest of it clicked")
{
    Desktop desktop;
    SliderStyle style;
    style.thumb_width = 20;
    Slider& slider = desktop.root().add<Slider>("s", style);
    slider.placement = top_left(0, 0, 120, 20); // Thumb at 0-20 for the minimum
    desktop.layout(kView);
    desktop.update(at(10, 10));
    CHECK(desktop.cursor() == CursorKind::Grab);
    desktop.update(at(80, 10));
    CHECK(desktop.cursor() == CursorKind::Hand);
    desktop.update(at(80, 10, true));
    desktop.update(at(90, 10, true));
    CHECK(desktop.cursor() == CursorKind::Grabbing);
}

TEST_CASE("a scroll bar's rider is taken hold of, its arrows and rail clicked")
{
    Desktop desktop;
    ScrollBarStyle style;
    style.thickness = 10;
    ScrollBar& bar = desktop.root().add<ScrollBar>("bar", style, Orientation::Vertical);
    bar.placement = top_left(0, 0, 10, 120); // Arrows 0-10 and 110-120, rider 10-35
    bar.content = 400;
    bar.visible = 100;
    desktop.layout(kView);
    const auto over = [&](float y) {
        desktop.update(at(5, y));
        return desktop.cursor();
    };
    CHECK(over(20) == CursorKind::Grab);
    CHECK(over(5) == CursorKind::Hand);
    CHECK(over(80) == CursorKind::Hand);

    SUBCASE("dragging the rider holds it")
    {
        desktop.update(at(5, 20, true));
        desktop.update(at(5, 60, true));
        CHECK(desktop.cursor() == CursorKind::Grabbing);
    }
    SUBCASE("holding an arrow stays a hand")
    {
        desktop.update(at(5, 5, true));
        desktop.update(at(5, 60, true));
        CHECK(desktop.cursor() == CursorKind::Hand);
    }
    SUBCASE("with nothing to scroll it is the arrow")
    {
        bar.content = 50;
        CHECK(over(20) == CursorKind::Arrow);
    }
}
