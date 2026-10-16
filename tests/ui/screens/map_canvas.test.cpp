// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/map_canvas.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;
using namespace rct3::ui::map_view;

TEST_CASE("the whole map fits the area keeping its shape, centred")
{
    const Rect wide { 0, 0, 400, 200 };
    const Rect square = map_rect(wide, 1.0f, { });
    CHECK(square.x0 == doctest::Approx(100));
    CHECK(square.x1 == doctest::Approx(300));
    CHECK(square.y0 == doctest::Approx(0));
    CHECK(square.y1 == doctest::Approx(200));

    const Rect tall { 10, 10, 110, 410 };
    const Rect flat = map_rect(tall, 2.0f, { });
    CHECK(flat.width() == doctest::Approx(100));
    CHECK(flat.height() == doctest::Approx(50));
    CHECK((flat.y0 + flat.y1) / 2 == doctest::Approx(210));
}

TEST_CASE("map and screen points convert both ways")
{
    const Rect map { 100, 50, 300, 250 };
    const Point at = to_map(map, { 150, 200 });
    CHECK(at.x == doctest::Approx(0.25f));
    CHECK(at.y == doctest::Approx(0.75f));
    const Point back = to_screen(map, at);
    CHECK(back.x == doctest::Approx(150));
    CHECK(back.y == doctest::Approx(200));
}

TEST_CASE("zooming keeps the map point under the pointer where it is")
{
    const Rect area { 0, 0, 300, 300 };
    const Point pointer { 210, 90 };
    const Point before = to_map(map_rect(area, 1, { }), pointer);
    const View in = zoomed(area, 1, { }, 2.0f, pointer);
    CHECK(in.zoom == doctest::Approx(2));
    const Point after = to_map(map_rect(area, 1, in), pointer);
    CHECK(after.x == doctest::Approx(before.x));
    CHECK(after.y == doctest::Approx(before.y));

    // Zoomed out past the whole map it stays whole and centred.
    const View out = zoomed(area, 1, in, 0.1f, pointer);
    CHECK(out.zoom == 1);
    CHECK(out.centre.x == doctest::Approx(0.5f));
    CHECK(out.centre.y == doctest::Approx(0.5f));
    CHECK(zoomed(area, 1, { }, 100.0f, pointer).zoom == kMaxZoom);
}

TEST_CASE("a zoomed view does not show beyond the map")
{
    const Rect area { 0, 0, 200, 100 };
    // A square map in a wide area: across, it never fills the area at zoom 2, so it stays centred.
    const View v = clamped(area, 1, { { 0.0f, 0.0f }, 2 });
    CHECK(v.centre.x == doctest::Approx(0.5f));
    CHECK(v.centre.y == doctest::Approx(0.25f));
}

TEST_CASE("zoomed in, the view follows a point that nears its edge")
{
    const Rect area { 0, 0, 100, 100 };
    const View v { { 0.5f, 0.5f }, 4 };
    // A quarter of the map shows; a point in its middle needs no move.
    CHECK(following(area, 1, v, { 0.5f, 0.5f }).centre.x == doctest::Approx(0.5f));
    const View moved = following(area, 1, v, { 0.8f, 0.5f });
    const Rect map = map_rect(area, 1, moved);
    const Point at = to_screen(map, { 0.8f, 0.5f });
    CHECK(at.x <= area.x1 - kFollowMargin * area.width() + 1e-3f);
    CHECK(moved.centre.y == doctest::Approx(0.5f));
    // The whole map showing, nothing moves.
    CHECK(following(area, 1, { }, { 0.95f, 0.05f }).centre.x == doctest::Approx(0.5f));
}
