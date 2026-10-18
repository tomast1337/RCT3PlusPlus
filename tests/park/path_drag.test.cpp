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

TEST_CASE("a brush stroke fills the tiles between two looks at the pointer, side by side")
{
    CHECK(brush_line({ 3, 2 }, { 1, 2 }) == std::vector<PathSpot> { { 3, 2 }, { 2, 2 }, { 1, 2 } });
    CHECK(brush_line({ 1, 1 }, { 1, 1 }) == std::vector<PathSpot> { { 1, 1 } });
    const std::vector<PathSpot> diagonal = brush_line({ 0, 0 }, { 2, 2 });
    CHECK(diagonal.size() == 5);
    for (std::size_t i = 1; i < diagonal.size(); ++i)
        CHECK(std::abs(diagonal[i].col - diagonal[i - 1].col) + std::abs(diagonal[i].row - diagonal[i - 1].row) == 1);
    CHECK(diagonal.back() == PathSpot { 2, 2 });
    // A shallow line stays near the straight one.
    for (const PathSpot spot : brush_line({ 0, 0 }, { 6, 1 }))
        CHECK((spot.row == 0 ? spot.col <= 3 : spot.col >= 3));
}

TEST_CASE("the nearest side of a tile is found by its diagonals")
{
    CHECK(nearest_side(0.5f, 0.9f) == 0);
    CHECK(nearest_side(0.9f, 0.5f) == 1);
    CHECK(nearest_side(0.5f, 0.1f) == 2);
    CHECK(nearest_side(0.1f, 0.5f) == 3);
}

TEST_CASE("the pointer over a raised path picks it and its edge; the next piece goes on from there")
{
    const save::TerrainGrid g = owned_grid(4, 4);
    save::PathTile raised = ground_path({ 1, 1 }, "Old", 1);
    raised.kind = save::PathKind::Flying;
    raised.elevation = save::PathElevation { };
    raised.elevation->quantised_height = 3;
    const std::vector<save::PathTile> paths { raised };
    // Looking straight down near the tile's higher-column edge.
    const Ray ray { { g.world_x(1) + 3.5f, 20.0f, g.world_z(1) + 2.0f }, { 0.0f, -1.0f, 0.0f } };
    const auto under = path_under(ray, paths, g);
    REQUIRE(under);
    CHECK(under->path == 0);
    CHECK(under->side == std::uint8_t { 1 });

    FootpathBuilder b;
    REQUIRE(start_from_edge(b, raised, g, 1));
    const auto next = next_piece(b, g);
    REQUIRE(next);
    CHECK(next->spot == PathSpot { 2, 1 });
    CHECK(next->start == 3);
    CHECK_FALSE(next->ground);
}
TEST_CASE("a path in a tunnel is picked only when the ground is seen through")
{
    save::TerrainGrid g = owned_grid(4, 4);
    g.cells[1 * 4 + 1].corners = { 8, 8, 8, 8 };
    save::PathTile tunnel = ground_path({ 1, 1 }, "Old", 1);
    tunnel.kind = save::PathKind::Flying;
    tunnel.elevation = save::PathElevation { };
    tunnel.elevation->quantised_height = 2;
    tunnel.elevation->underground = true;
    const std::vector<save::PathTile> paths { tunnel };
    const Ray ray { { g.world_x(1) + 2.0f, 20.0f, g.world_z(1) + 2.0f }, { 0.0f, -1.0f, 0.0f } };
    CHECK_FALSE(path_under(ray, paths, g));
    const auto under = path_under(ray, paths, g, true);
    REQUIRE(under);
    CHECK(under->path == 0);
}

TEST_CASE("a stroke lays only where a path can lie on the ground")
{
    save::TerrainGrid g = owned_grid(6, 3);
    g.cells[1 * 6 + 2].corners = { 0, 0, 4, 4 }; // A steep tile
    g.cells[1 * 6 + 4].flags = 0;                // Not the park's
    const std::vector<save::PathTile> paths { ground_path({ 3, 1 }, "Old", 1) };
    const DragPlan plan = plan_drag(brush_line({ 0, 1 }, { 5, 1 }), g, paths);
    CHECK(plan.lay == std::vector<PathSpot> { { 0, 1 }, { 1, 1 }, { 5, 1 } });
    CHECK(plan.kept == 1);
    CHECK(plan.refused == 2);
}
