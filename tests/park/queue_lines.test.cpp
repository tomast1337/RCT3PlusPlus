// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/queue_lines.hpp"

#include "park/footpath_builder.hpp"
#include "park/led_signs.hpp"
#include "park/path_decks.hpp"
#include "park/queue_fences.hpp"

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
    std::vector<save::PathTile> lay_queue(const save::TerrainGrid& g, save::Tile from, std::uint8_t heading, int count)
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
    CHECK(entrance_beside(rides, { 2, 2 }, 0) == EntranceBeside { 0, 0, 1 });
    CHECK(entrance_beside(rides, { 3, 3 }, std::nullopt) == EntranceBeside { 0, 0, 2 });
    CHECK_FALSE(entrance_beside(rides, { 2, 3 }, 0));
    CHECK_FALSE(entrance_beside(rides, { 2, 2 }, kEntranceReach + 1));
}

TEST_CASE("a queue laid away from an entrance joins the station's line")
{
    const save::TerrainGrid g = owned_grid(8, 4);
    save::RideModel rides = ride_at(0, 1);
    // Heading +col from the tile beside the entrance.
    std::vector<save::PathTile> paths = lay_queue(g, { 1, 1 }, 1, 3);
    CHECK(tiles_behind(paths, g, 2) == std::vector<std::size_t> { 1, 0 });
    const auto entrance = entrance_beside(rides, { 1, 1 }, 0);
    REQUIRE(entrance);
    rides.rides[0].fence_symbol = "Picket";
    rides.rides[0].fence_overlay = "Fences";
    rides.rides[0].fence_colours[0] = 5;
    CHECK(join_entrance(rides, paths, g, 0, *entrance, 99) == 99);
    CHECK(rides.rides[0].stations[0].queue_line == 99);
    CHECK(rides.rides[0].stations[0].queue_tiles == 1);
    // The line is railed with its ride's fence.
    CHECK(paths[0].queue->fence_symbol == "Picket");
    CHECK(paths[0].queue->fence_overlay == "Fences");
    CHECK(paths[0].queue->fence_colours[0] == 5);
    CHECK(paths[2].queue->fence_symbol.empty());
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
    const auto entrance = entrance_beside(rides, { 4, 1 }, 0);
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

TEST_CASE("a queue's open end turns to a path beside it")
{
    const save::TerrainGrid g = owned_grid(8, 8);
    std::vector<save::PathTile> paths = lay_queue(g, { 1, 1 }, 1, 3); // (1..3, 1), open towards (4, 1)
    paths.push_back(ground_path({ 3, 2 }, "Tarmac", 50));
    paths.push_back(ground_path({ 2, 0 }, "Tarmac", 51));
    CHECK_FALSE(turn_to_path(paths, g, 1)); // Its start meets the next queue tile
    REQUIRE(turn_to_path(paths, g, 2));
    CHECK(stored_direction(paths[2].queue->start_direction) == 0);
    CHECK_FALSE(turn_to_path(paths, g, 2)); // Now it meets the path

    // Nor does it turn while something meets its open end already.
    std::vector<save::PathTile> met = lay_queue(g, { 1, 1 }, 1, 3);
    met.push_back(ground_path({ 4, 1 }, "Tarmac", 50));
    met.push_back(ground_path({ 3, 2 }, "Tarmac", 51));
    CHECK_FALSE(turn_to_path(met, g, 2));
}

TEST_CASE("a queue laid on towards a line's open end joins it, turned round to lead in")
{
    const save::TerrainGrid g = owned_grid(8, 4);
    save::RideModel rides = ride_at(5, 1);
    std::vector<save::PathTile> paths = lay_queue(g, { 2, 1 }, 1, 3); // (2..4, 1), the entrance beyond
    const std::uint64_t line = join_entrance(rides, paths, g, 2, *entrance_beside(rides, { 4, 1 }, 0), 99);
    // A second stroke from (0, 1) up to the line's open end at (2, 1).
    for (save::PathTile p : lay_queue(g, { 0, 1 }, 1, 2))
    {
        p.id += 100;
        paths.push_back(p);
    }
    CHECK_FALSE(join_line(rides, paths, g, 3)); // (0, 1) is not beside the line's end
    CHECK(join_line(rides, paths, g, 4) == line);
    for (const save::PathTile& p : paths)
    {
        CHECK(p.queue->queue_line_ref == line);
        CHECK(p.queue->end_direction == stored_direction(1));
    }
    CHECK(rides.rides[0].stations[0].queue_tiles == 5);
    CHECK_FALSE(join_line(rides, paths, g, 4)); // Already on it
}

TEST_CASE("a queue over steps laid towards the entrance turns round whole, its steps the same way up")
{
    const save::TerrainGrid g = owned_grid(8, 4);
    save::RideModel rides = ride_at(5, 1);
    FootpathBuilder b;
    b.queue = true;
    b.heading = 1;
    start_at(b, { 1, 1 });
    std::vector<save::PathTile> paths;
    for (const PathSlope slope : { PathSlope::Flat, PathSlope::SteepUp, PathSlope::Flat, PathSlope::SteepDown })
    {
        b.slope = slope;
        const auto piece = next_piece(b, g);
        REQUIRE(piece);
        REQUIRE(lay_path(b, paths, *piece, "QueueSet", 10 + paths.size(), 50 + paths.size()));
    }
    REQUIRE(paths.size() == 4);
    REQUIRE(paths.back().col == 4);
    for (save::PathTile& p : paths)
    {
        p.queue->fence_symbol = "Rail";
        p.queue->fence_overlay = "Fences/Rail";
    }
    const QueueFences fenced = queue_fences(paths, g, 0);
    std::vector<std::array<std::optional<float>, 4>> before;
    for (const save::PathTile& p : paths)
        before.push_back({ edge_height(p, g, 0), edge_height(p, g, 1), edge_height(p, g, 2), edge_height(p, g, 3) });

    join_entrance(rides, paths, g, 3, *entrance_beside(rides, { 4, 1 }, std::nullopt), 99);
    // The sloping pieces' ends stay where they were.
    for (const std::size_t i : { 1u, 3u })
    {
        CHECK(edge_height(paths[i], g, 1) == before[i][1]);
        CHECK(edge_height(paths[i], g, 3) == before[i][3]);
    }
    // Their railings lean the same way as before.
    const QueueFences refenced = queue_fences(paths, g, 0);
    REQUIRE(fenced.leans.size() == 4); // Two along each flight
    REQUIRE(refenced.leans.size() == fenced.leans.size());
    for (const auto& [id, lean] : fenced.leans)
    {
        CHECK(refenced.leans.at(id).x == doctest::Approx(lean.x));
        CHECK(refenced.leans.at(id).y == doctest::Approx(lean.y));
    }
    // The line runs unbroken from its outer end to the entrance, and its sign stands at that end.
    CHECK(tiles_behind(paths, g, 0).size() == 3);
    const auto spots = queue_sign_spots(paths, g);
    REQUIRE(spots.size() == 1);
    CHECK(spots[0].col == 1);
    CHECK(spots[0].turns == edge_turns(TileSide::Left));
}
