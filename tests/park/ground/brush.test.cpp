// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground/brush.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;
using namespace rct3::park::ground;

TEST_CASE("the tile tools take a square of tiles as wide as the brush, inside the grid")
{
    CHECK(brush_across(1, 4) == 1);
    CHECK(brush_across(10, 4) == 5);
    CHECK(brush_across(kLargestBrush, 4) == 32);
    CHECK(brush_tiles({ 5, 5 }, 1, 20, 20) == std::vector<save::model::Tile> { { 5, 5 } });
    CHECK(brush_tiles({ 5, 5 }, 3, 20, 20).size() == 9);
    const auto even = brush_tiles({ 5, 5 }, 4, 20, 20);
    CHECK(even.size() == 16);
    CHECK(even.front() == save::model::Tile { 4, 4 }); // One further right and down
    CHECK(even.back() == save::model::Tile { 7, 7 });
    CHECK(brush_tiles({ 0, 0 }, 3, 20, 20).size() == 4); // Cut at the grid's edge
}

TEST_CASE("a round brush paints the tile under the pointer and those with a corner within its radius")
{
    // Too small to reach a corner: the pointer's tile alone.
    CHECK(round_brush_tiles({ 5.5f, 5.5f }, 0.25f, 20, 20) == std::vector<save::model::Tile> { { 5, 5 } });
    // Near a corner: the four tiles that share it.
    CHECK(round_brush_tiles({ 5.1f, 5.1f }, 0.25f, 20, 20).size() == 4);
    const auto round = round_brush_tiles({ 5.5f, 5.5f }, 2.5f, 20, 20);
    CHECK(std::ranges::contains(round, save::model::Tile { 7, 5 }));       // Its nearest corners are 1.6 away
    CHECK_FALSE(std::ranges::contains(round, save::model::Tile { 8, 5 })); // 2.55 away
    CHECK_FALSE(std::ranges::contains(round, save::model::Tile { 8, 8 })); // Its nearest corner is 3.5 away
    CHECK(std::ranges::contains(round, save::model::Tile { 7, 7 }));       // Nearest corner 2.1 away
    CHECK(round_brush_tiles({ 0.5f, 0.5f }, 1, 20, 20).size() == 4);       // Cut at the grid's edge
}
