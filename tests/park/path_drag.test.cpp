// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path_drag.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid owned_grid(int width, int height)
    {
        save::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(width * height));
        for (save::TerrainCell& c : g.cells)
            c.flags = static_cast<std::uint8_t>(save::TileFlag::Owned);
        return g;
    }
} // namespace

TEST_CASE("a stroke covers the rectangle between its ends, on the map and within reach")
{
    const save::TerrainGrid g = owned_grid(100, 100);
    CHECK(drag_area({ 3, 2 }, { 1, 2 }, g) == std::vector<PathSpot> { { 1, 2 }, { 2, 2 }, { 3, 2 } });
    CHECK(drag_area({ 1, 1 }, { 2, 2 }, g).size() == 4);
    CHECK(drag_area({ 98, 0 }, { 120, 0 }, g).size() == 2);
    CHECK(drag_area({ 0, 0 }, { 99, 0 }, g).size() == static_cast<std::size_t>(kLongestStroke));
}

TEST_CASE("a stroke lays only where a path can lie on the ground")
{
    save::TerrainGrid g = owned_grid(6, 3);
    g.cells[1 * 6 + 2].corners = { 0, 0, 4, 4 }; // A steep tile
    g.cells[1 * 6 + 4].flags = 0;                // Not the park's
    const std::vector<save::PathTile> paths { ground_path({ 3, 1 }, "Old", 1) };
    const DragPlan plan = plan_drag(drag_area({ 0, 1 }, { 5, 1 }, g), g, paths);
    CHECK(plan.lay == std::vector<PathSpot> { { 0, 1 }, { 1, 1 }, { 5, 1 } });
    CHECK(plan.kept == 1);
    CHECK(plan.refused == 2);
}
