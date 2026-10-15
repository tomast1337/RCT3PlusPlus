// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/shape.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

namespace
{
    constexpr Color kRed { 255, 0, 0, 255 };
    constexpr Color kBlue { 0, 0, 255, 255 };

    Shape square(float side, float radius)
    {
        Shape s;
        s.rect = { 0, 0, side, side };
        s.corners = round_corners(radius);
        return s;
    }
} // namespace

TEST_CASE("the distance to a shape's edge is negative inside, zero on it and positive outside")
{
    SUBCASE("a square")
    {
        const ShapeMetrics m = shape_metrics(square(20, 0), 1);
        CHECK(shape_distance(m, 0, 0) == doctest::Approx(-10));
        CHECK(shape_distance(m, 10, 0) == doctest::Approx(0));
        CHECK(shape_distance(m, 13, 0) == doctest::Approx(3));
        // Past a sharp corner, straight to the corner.
        CHECK(shape_distance(m, 13, 14) == doctest::Approx(5));
    }
    SUBCASE("a disc: a square rounded by half its side")
    {
        const ShapeMetrics m = shape_metrics(square(20, 10), 1);
        CHECK(shape_distance(m, 0, 0) == doctest::Approx(-10));
        CHECK(shape_distance(m, 6, 8) == doctest::Approx(0));
        CHECK(shape_distance(m, 12, 16) == doctest::Approx(10));
    }
    SUBCASE("each corner keeps its own radius")
    {
        Shape s = square(20, 0);
        s.corners.top_right = 10;
        const ShapeMetrics m = shape_metrics(s, 1);
        // Round at the top-right, square at the bottom-left.
        CHECK(shape_distance(m, 9, -9) > 0);
        CHECK(shape_distance(m, -9, 9) < 0);
    }
    SUBCASE("a ring is filled only within its width of the edge")
    {
        Shape s = square(20, 10);
        s.ring = 4;
        const ShapeMetrics m = shape_metrics(s, 1);
        CHECK(shape_distance(m, 0, 0) == doctest::Approx(6));
        CHECK(shape_distance(m, 8, 0) == doctest::Approx(-2));
        CHECK(shape_distance(m, 6, 0) == doctest::Approx(0));
    }
}

TEST_CASE("measures scale with the list and radii stop at half the shorter side")
{
    Shape s;
    s.rect = { 0, 0, 40, 10 };
    s.corners = { 3, 100, 0, -1 };
    s.softness = 2;
    const ShapeMetrics m = shape_metrics(s, 2);
    CHECK(m.half_width == 40);
    CHECK(m.half_height == 10);
    CHECK(m.corners.top_left == 6);
    CHECK(m.corners.top_right == 10);
    CHECK(m.corners.bottom_left == 0);
    CHECK(m.softness == 4);
    CHECK(shape_margin(m) == 3);
}

TEST_CASE("a crisp edge covers half a pixel on the edge, a soft one spreads wider")
{
    ShapeMetrics crisp;
    CHECK(shape_coverage(crisp, 0) == doctest::Approx(0.5f));
    CHECK(shape_coverage(crisp, -0.5f) == 1);
    CHECK(shape_coverage(crisp, 0.5f) == 0);
    ShapeMetrics soft;
    soft.softness = 8;
    CHECK(shape_coverage(soft, 0) == doctest::Approx(0.5f));
    CHECK(shape_coverage(soft, 2) > 0);
    CHECK(shape_coverage(soft, 2) < 0.5f);
    CHECK(shape_coverage(soft, 4) == 0);
}

TEST_CASE("a shape's colour runs from top to bottom, under its outline and light")
{
    Shape s = square(20, 0);
    s.top = kRed;
    s.bottom = kBlue;

    SUBCASE("the fill")
    {
        const ShapeMetrics m = shape_metrics(s, 1);
        CHECK(shade_shape(s, m, 0, -10 + 0.01f).r > 250);
        CHECK(shade_shape(s, m, 0, 0) == Color { 128, 0, 128, 255 });
        CHECK(shade_shape(s, m, 0, 9.99f).b > 250);
        CHECK(shade_shape(s, m, 30, 0).a == 0);
    }
    SUBCASE("the outline, along the inside of the edge")
    {
        s.outline = kWhite;
        s.outline_width = 2;
        const ShapeMetrics m = shape_metrics(s, 1);
        CHECK(shade_shape(s, m, -9, 0) == Color { 255, 255, 255, 255 });
        CHECK(shade_shape(s, m, -5, 0) == Color { 128, 0, 128, 255 });
    }
    SUBCASE("the light, on edges that face up")
    {
        s.top = kBlue;
        s.light = kWhite;
        s.light_width = 4;
        const ShapeMetrics m = shape_metrics(s, 1);
        CHECK(shade_shape(s, m, 0, -9.5f).r > 200);
        CHECK(shade_shape(s, m, 0, 9.5f).r == 0);
        CHECK(shade_shape(s, m, -9.5f, 0).r == 0);
    }
    SUBCASE("a soft edge fades the alpha")
    {
        s.softness = 6;
        const ShapeMetrics m = shape_metrics(s, 1);
        const Color edge = shade_shape(s, m, 10, 0);
        CHECK(edge.a > 100);
        CHECK(edge.a < 155);
        CHECK(shade_shape(s, m, 0, 0).a == 255);
    }
}
