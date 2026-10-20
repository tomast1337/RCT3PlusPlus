// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_brush.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

TEST_CASE("a land brush covers a square or a round of tiles round the pointer's, inside the grid")
{
    CHECK(brush_tiles({ 5, 5 }, 1, false, 20, 20) == std::vector<save::Tile> { { 5, 5 } });
    CHECK(brush_tiles({ 5, 5 }, 1, true, 20, 20) == std::vector<save::Tile> { { 5, 5 } });
    CHECK(brush_tiles({ 5, 5 }, 3, false, 20, 20).size() == 9);
    CHECK(brush_tiles({ 5, 5 }, 4, false, 20, 20).size() == 16);

    const auto round = brush_tiles({ 5, 5 }, 5, true, 20, 20);
    CHECK(round.size() < 25);
    CHECK(std::ranges::contains(round, save::Tile { 7, 5 }));       // The edge in line with the middle
    CHECK_FALSE(std::ranges::contains(round, save::Tile { 7, 7 })); // The corner falls outside
    CHECK(brush_tiles({ 5, 5 }, 4, true, 20, 20).size() == 12);     // Four by four less its corners

    CHECK(brush_tiles({ 0, 0 }, 3, false, 20, 20).size() == 4); // Cut at the grid's edge
    CHECK(brush_tiles({ 20, 20 }, 99, false, 50, 50).size() == kLargestBrush * kLargestBrush);
}
