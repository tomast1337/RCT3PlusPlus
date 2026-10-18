// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ride_placement.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{

    // Eight by eight, all owned, the ground at height 2 but for a hill on the tile at 6,6.
    save::TerrainGrid owned_land()
    {
        save::TerrainGrid g;
        g.width = 8;
        g.height = 8;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(64);
        for (save::TerrainCell& c : g.cells)
        {
            c.flags = std::to_underlying(save::TileFlag::Owned);
            c.corners = { 2, 2, 2, 2 };
        }
        g.at(6, 6).corners = { 5, 6, 6, 7 };
        return g;
    }

} // namespace

TEST_CASE("a ride's footprint is centred on the tile under the pointer, turned, at the ground's height")
{
    const save::TerrainGrid grid = owned_land();
    const RideSite flat = ride_site(4, 4, { 3, 2 }, 0, grid);
    CHECK(flat.col == 3);
    CHECK(flat.row == 4);
    CHECK(flat.span == std::array<std::int32_t, 2> { 3, 2 });
    CHECK(flat.height == 2);
    const RideSite turned = ride_site(4, 4, { 3, 2 }, 5, grid);
    CHECK(turned.turns == 1);
    CHECK(turned.span == std::array<std::int32_t, 2> { 2, 3 });
    CHECK(turned.col == 4);
    CHECK(turned.row == 3);
    CHECK(ride_site(6, 6, { 1, 1 }, 0, grid).height == 6);
}

TEST_CASE("a ride needs owned land clear of paths and of solid things above its base")
{
    save::TerrainGrid grid = owned_land();
    const RideSite site = ride_site(2, 2, { 3, 3 }, 0, grid);
    CHECK(site_problem(site, grid, { }, { }) == SiteProblem::None);
    CHECK(site_problem(ride_site(0, 0, { 3, 3 }, 0, grid), grid, { }, { }) == SiteProblem::NotOwned);
    grid.at(3, 1).flags |= std::to_underlying(save::TileFlag::RightsOnly);
    CHECK(site_problem(site, grid, { }, { }) == SiteProblem::NotOwned);
    grid = owned_land();
    save::PathTile path;
    path.col = 1;
    path.row = 3;
    CHECK(site_problem(site, grid, std::span(&path, 1), { }) == SiteProblem::Path);
    SolidTiles solids;
    solids.tiles[3 << 16 | 2] = { SolidSpan { 0, 1 } }; // Under the base: no matter
    CHECK(site_problem(site, grid, { }, solids) == SiteProblem::None);
    solids.tiles[3 << 16 | 2].push_back({ 1, 4 });
    CHECK(site_problem(site, grid, { }, solids) == SiteProblem::Blocked);
}

TEST_CASE("placing a ride levels its footprint, and the tiles round it slope to it")
{
    save::TerrainGrid grid = owned_land();
    RideSite site = ride_site(6, 6, { 1, 1 }, 0, grid);
    site.height = 4;
    level_ground(grid, site);
    CHECK(grid.at(6, 6).corners == std::array<float, 4> { 4, 4, 4, 4 });
    // The tile to its left shares its two left corners; its own far ones stay.
    CHECK(grid.at(5, 6).corners == std::array<float, 4> { 2, 4, 2, 4 });
    // The tile diagonally up and left shares one corner.
    CHECK(grid.at(5, 5).corners == std::array<float, 4> { 2, 2, 2, 4 });
    CHECK(grid.at(4, 6).corners == std::array<float, 4> { 2, 2, 2, 2 });
}

TEST_CASE("an entrance goes across the footprint's rim nearest the pointer, facing the ride")
{
    const save::TerrainGrid grid = owned_land();
    const RideSite site = ride_site(3, 3, { 3, 3 }, 0, grid); // Tiles 2..4 both ways
    CHECK(door_spot(site, 0, 3) == DoorSpot { 1, 3, 2 });
    CHECK(door_spot(site, 3, 7) == DoorSpot { 3, 5, 3 });
    CHECK(door_spot(site, 6, 2) == DoorSpot { 5, 2, 0 });
    CHECK(door_spot(site, 4, 0) == DoorSpot { 4, 1, 1 });
    // Inside, the nearest side; off a corner, never the corner tile.
    CHECK(door_spot(site, 4, 3) == DoorSpot { 5, 3, 0 });
    const DoorSpot corner = door_spot(site, 0, 0);
    CHECK(((corner.col == 1 && corner.row == 2) || (corner.col == 2 && corner.row == 1)));
    // Not on the other door's tile.
    const DoorSpot entrance = door_spot(site, 0, 3);
    CHECK(door_problem(site, entrance, std::nullopt, grid, { }, { }) == SiteProblem::None);
    CHECK(door_problem(site, entrance, entrance, grid, { }, { }) == SiteProblem::Blocked);
}
