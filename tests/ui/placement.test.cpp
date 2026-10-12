// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/placement.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("placements follow the parent")
{
    const Rect small { 0, 0, 200, 100 };
    const Rect large { 10, 20, 410, 320 };

    SUBCASE("fill stretches with a margin")
    {
        CHECK(place(fill(5), small) == Rect { 5, 5, 195, 95 });
        CHECK(place(fill(5), large) == Rect { 15, 25, 405, 315 });
    }
    SUBCASE("corner placements keep their size and distance to the corner")
    {
        CHECK(place(top_left(4, 6, 30, 10), large) == Rect { 14, 26, 44, 36 });
        CHECK(place(top_right(4, 6, 30, 10), small) == Rect { 166, 6, 196, 16 });
        CHECK(place(top_right(4, 6, 30, 10), large) == Rect { 376, 26, 406, 36 });
        CHECK(place(bottom_left(4, 6, 30, 10), small) == Rect { 4, 84, 34, 94 });
        CHECK(place(bottom_right(4, 6, 30, 10), small) == Rect { 166, 84, 196, 94 });
    }
    SUBCASE("centred stays in the middle")
    {
        CHECK(place(centred(40, 20), small) == Rect { 80, 40, 120, 60 });
        CHECK(place(centred(40, 20, 5, -5), large) == Rect { 195, 155, 235, 175 });
    }
    SUBCASE("a band stretches across")
    {
        CHECK(place(top_band(10, 20, 2), small) == Rect { 2, 10, 198, 30 });
    }
}

TEST_CASE("moved_inside brings a rectangle back into bounds")
{
    const Rect bounds { 0, 0, 100, 100 };
    CHECK(moved_inside({ 10, 10, 20, 20 }, bounds) == Rect { 10, 10, 20, 20 });
    CHECK(moved_inside({ 90, -5, 120, 10 }, bounds) == Rect { 70, 0, 100, 15 });
    CHECK(moved_inside({ -10, 95, 10, 110 }, bounds) == Rect { 0, 85, 20, 100 });
    // Too big: the top-left corner wins.
    CHECK(moved_inside({ -20, -20, 150, 150 }, bounds) == Rect { 0, 0, 170, 170 });
}
