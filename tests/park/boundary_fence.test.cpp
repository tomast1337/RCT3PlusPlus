// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/boundary_fence.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{

    // Three by three; the park owns the middle tile and the one right of it, and only the rights to the one
    // below the middle.
    save::TerrainGrid small_park()
    {
        save::TerrainGrid g;
        g.width = 3;
        g.height = 3;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(9);
        g.cells[1 * 3 + 1].flags = std::to_underlying(save::TileFlag::Owned);
        g.cells[1 * 3 + 2].flags = std::to_underlying(save::TileFlag::Owned);
        g.cells[2 * 3 + 1].flags = std::to_underlying(save::TileFlag::Owned) | std::to_underlying(save::TileFlag::RightsOnly);
        return g;
    }

    save::PathTile path_at(int col, int row)
    {
        save::PathTile p;
        p.col = col;
        p.row = row;
        return p;
    }

} // namespace

TEST_CASE("the park fence runs round the land the park owns outright, the map's edge included")
{
    const BoundaryFence fence = boundary_fence("Chain", "Fences/Chain", small_park(), { }, 0);
    // Two tiles side by side: six edges, one of them on the map's edge.
    REQUIRE(fence.pieces.size() == 6);
    for (const save::PlacedObject& o : fence.pieces)
    {
        CHECK(o.symbol == "Chain");
        CHECK(o.fixture);
        CHECK(o.overlay == "Fences/Chain");
        CHECK((o.col == 1 || o.col == 2));
        CHECK(o.row == 1);
    }
    CHECK(fence.leans.empty()); // Flat ground
    CHECK(boundary_fence("", "", small_park(), { }, 0).pieces.empty());
}

TEST_CASE("a path across the park's edge leaves it open; one only on the inside does not")
{
    // Path on the middle tile and on the one above it, across its edge.
    const std::vector<save::PathTile> across { path_at(1, 1), path_at(1, 0) };
    CHECK(boundary_fence("Chain", "", small_park(), across, 0).pieces.size() == 5);
    const std::vector<save::PathTile> inside { path_at(1, 1) };
    CHECK(boundary_fence("Chain", "", small_park(), inside, 0).pieces.size() == 6);
}

TEST_CASE("a park fence panel on a slope leans with its edge")
{
    save::TerrainGrid g = small_park();
    g.cells[1 * 3 + 1].corners = { 0, 1, 0, 1 }; // Rising along x
    const BoundaryFence fence = boundary_fence("Chain", "", g, { }, 10);
    CHECK(fence.pieces.front().id == 11);
    CHECK_FALSE(fence.leans.empty());
}
