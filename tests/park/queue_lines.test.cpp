// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/queue_lines.hpp"

#include "park/footpath_builder.hpp"
#include "park/path_decks.hpp"

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

    /// One ride whose station's entrance stands on (`col`, `row`).
    save::RideModel ride_at(int col, int row)
    {
        save::RideModel rides;
        save::Station& station = rides.rides.emplace_back().stations.emplace_back();
        station.entrance_spot = save::PlacementSpot { };
        station.entrance_spot->col = col;
        station.entrance_spot->row = row;
        return rides;
    }

    /// Lays `count` ground queue tiles from `from` heading `heading`.
    std::vector<save::PathTile> lay_queue(const save::TerrainGrid& g, PathSpot from, std::uint8_t heading, int count)
    {
        FootpathBuilder b;
        b.queue = true;
        b.heading = heading;
        start_at(b, from);
        std::vector<save::PathTile> paths;
        for (int i = 0; i < count; ++i)
            (void)lay_path(b, paths, *next_piece(b, g), "QueueSet", static_cast<std::uint64_t>(10 + i), 0);
        return paths;
    }
} // namespace

TEST_CASE("an entrance beside a tile is found on its side, near its height")
{
    const save::RideModel rides = ride_at(3, 2);
    CHECK(entrance_beside(rides, 2, 2, 0) == EntranceBeside { 0, 0, 1 });
    CHECK(entrance_beside(rides, 3, 3, std::nullopt) == EntranceBeside { 0, 0, 2 });
    CHECK_FALSE(entrance_beside(rides, 2, 3, 0));
    CHECK_FALSE(entrance_beside(rides, 2, 2, kEntranceReach + 1));
}

TEST_CASE("a queue laid away from an entrance joins the station's line")
{
    const save::TerrainGrid g = owned_grid(8, 4);
    save::RideModel rides = ride_at(0, 1);
    // Heading +col from the tile beside the entrance.
    std::vector<save::PathTile> paths = lay_queue(g, { 1, 1 }, 1, 3);
    CHECK(tiles_behind(paths, g, 2) == std::vector<std::size_t> { 1, 0 });
    const auto entrance = entrance_beside(rides, 1, 1, 0);
    REQUIRE(entrance);
    CHECK(join_entrance(rides, paths, g, 0, *entrance, 99) == 99);
    CHECK(rides.rides[0].stations[0].queue_line == 99);
    CHECK(rides.rides[0].stations[0].queue_tiles == 1);
    CHECK(paths[0].queue->end_direction == stored_direction(3));
    // Tiles laid after join the line as they are laid; here they were laid first, so only the joined one has it.
    CHECK(paths[2].queue->queue_line_ref == 0);
}

TEST_CASE("a queue laid towards an entrance is turned round to lead to it")
{
    const save::TerrainGrid g = owned_grid(8, 4);
    save::RideModel rides = ride_at(5, 1);
    rides.rides[0].stations[0].queue_line = 7;
    std::vector<save::PathTile> paths = lay_queue(g, { 2, 1 }, 1, 3); // Ends on (4, 1), beside the entrance
    const auto entrance = entrance_beside(rides, 4, 1, 0);
    REQUIRE(entrance);
    CHECK(join_entrance(rides, paths, g, 2, *entrance, 99) == 7);
    for (const save::PathTile& p : paths)
    {
        CHECK(p.queue->queue_line_ref == 7);
        CHECK(p.queue->end_direction == stored_direction(1)); // All lead towards higher columns
    }
    CHECK(paths[0].queue->start_direction == stored_direction(3));
    CHECK(paths[1].queue->start_direction == stored_direction(3));
    CHECK(rides.rides[0].stations[0].queue_tiles == 3);
    // The head is the tile nothing leads into.
    CHECK(tiles_behind(paths, g, 2).size() == 0);
}
