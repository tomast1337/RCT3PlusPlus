// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/coded_frame.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    constexpr Color kRed { 255, 0, 0, 255 };

    FrameLayer layer(FrameBox box)
    {
        Shape s;
        s.top = kRed;
        s.bottom = kRed;
        return { box, s, { } };
    }

    // The bounds of the shape quads in `list`, in pixels.
    Rect bounds(const DrawList& list)
    {
        Rect r { 1e9f, 1e9f, -1e9f, -1e9f };
        for (const ShapeVertex& v : list.shape_vertices())
            r = { std::min(r.x0, v.x), std::min(r.y0, v.y), std::max(r.x1, v.x), std::max(r.y1, v.y) };
        return r;
    }
} // namespace

TEST_CASE("a layer's box is pinned to the frame edge by edge")
{
    const Rect target { 100, 50, 300, 150 };
    SUBCASE("insets keep their size whatever the frame's")
    {
        CHECK(frame_rect(inset_box(2, 3, 4, 5), target, 0, 0) == Rect { 102, 53, 296, 145 });
    }
    SUBCASE("a corner box keeps its size at the top-left")
    {
        CHECK(frame_rect(corner_box(10, 20, 30, 40), target, 0, 0) == Rect { 110, 70, 140, 110 });
    }
    SUBCASE("shares follow the frame's size")
    {
        const FrameBox middle { { 0.5f, -10 }, { 0, 0 }, { 0.5f, 10 }, { 1, 0 } };
        CHECK(frame_rect(middle, target, 0, 0) == Rect { 190, 50, 210, 150 });
    }
    SUBCASE("a frame with a size it was measured at scales its units")
    {
        // Measured at 100 by 50, drawn at twice that.
        CHECK(frame_rect(corner_box(10, 10, 20, 20), target, 100, 50) == Rect { 120, 70, 160, 110 });
    }
}

TEST_CASE("a coded frame draws its layers as shapes")
{
    CodedFrame frame;
    frame.layers = { layer(inset_box(0, 0, 0, 0)), layer(corner_box(0, 0, 10, 10)) };
    DrawList list;
    draw_frame(list, frame, { 0, 0, 100, 40 });
    CHECK(list.vertices().empty());
    CHECK(list.shape_vertices().size() == 8);
    REQUIRE(list.batches().size() == 1);
    CHECK(list.batches()[0].kind == BatchKind::Shapes);
}

TEST_CASE("a coded frame's layer can be cut to a part of the frame")
{
    CodedFrame frame;
    FrameLayer l = layer(inset_box(0, 0, 0, 0));
    l.clip = inset_box(0, 0, 50, 0); // The left half
    frame.layers = { l };
    DrawList list;
    draw_frame(list, frame, { 0, 0, 100, 40 });
    const Rect b = bounds(list);
    CHECK(b.x1 == doctest::Approx(50));
    CHECK(b.x0 == doctest::Approx(0));
    CHECK(!list.clip());
}

TEST_CASE("a tint multiplies a coded frame's colours as it would a picture's")
{
    CodedFrame frame;
    frame.layers = { layer(inset_box(0, 0, 0, 0)) };
    DrawList list;
    draw_frame(list, frame, { 0, 0, 10, 10 }, { 128, 128, 128, 255 });
    REQUIRE_FALSE(list.shape_vertices().empty());
    CHECK(list.shape_vertices()[0].top == std::array<std::uint8_t, 4> { 128, 0, 0, 255 });
}

TEST_CASE("a frame measured at a size scales its radii with the smaller of its two scales")
{
    CodedFrame frame;
    FrameLayer l = layer(corner_box(0, 0, 10, 10));
    l.shape.corners = round_corners(5);
    frame.layers = { l };
    frame.width = 10;
    frame.height = 10;
    DrawList list;
    draw_frame(list, frame, { 0, 0, 40, 20 }); // Four times as wide, twice as high
    REQUIRE_FALSE(list.shape_vertices().empty());
    CHECK(list.shape_vertices()[0].radii[0] == doctest::Approx(10));
}
