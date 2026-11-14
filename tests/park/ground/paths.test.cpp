// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground/paths.hpp"

#include <doctest/doctest.h>
#include <set>

using namespace rct3;
using namespace rct3::park;
using namespace rct3::park::ground;

namespace
{

    save::model::TerrainGrid flat_grid(int width, int height, float level = 0)
    {
        save::model::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.origin_x = -6;
        g.origin_z = 2;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(width * height));
        for (save::model::TerrainCell& c : g.cells)
            c.corners = { level, level, level, level };
        return g;
    }

    save::model::PathTile ground_tile(int col, int row, std::string style = "Gravel")
    {
        save::model::PathTile p;
        p.kind = save::model::PathKind::Ground;
        p.col = static_cast<std::uint8_t>(col);
        p.row = static_cast<std::uint8_t>(row);
        p.surface_name = std::move(style);
        return p;
    }

    std::size_t vertices_of(const TerrainMesh& m, GroundPaint paint)
    {
        std::size_t n = 0;
        for (const GroundBatch& b : m.batches)
            n += b.paint == paint ? b.count : 0;
        return n;
    }

} // namespace

TEST_CASE("a quarter's look follows the path round its corner")
{
    CHECK(quarter_look({ .along_x = true, .along_z = true, .across = true }) == PathLook::Open);
    CHECK(quarter_look({ .along_x = true, .along_z = true }) == PathLook::Notched);
    CHECK(quarter_look({ .along_z = true }) == PathLook::EdgeX);
    CHECK(quarter_look({ .along_z = true, .across = true }) == PathLook::EdgeXJoined);
    CHECK(quarter_look({ .along_x = true }) == PathLook::EdgeZ);
    CHECK(quarter_look({ .along_x = true, .across = true }) == PathLook::EdgeZJoined);
    CHECK(quarter_look({ }) == PathLook::Closed);
    CHECK(quarter_look({ .across = true }) == PathLook::Closed);
}

TEST_CASE("every look has a cell of its own")
{
    std::set<std::pair<int, int>> seen;
    for (const PathLook look : { PathLook::Open, PathLook::Notched, PathLook::EdgeX, PathLook::EdgeXJoined, PathLook::EdgeZ, PathLook::EdgeZJoined, PathLook::Closed, PathLook::CornerOnly })
    {
        const PathCell c = path_cell(look);
        CHECK(c.sheet < 2);
        CHECK(c.cell < 4);
        seen.emplace(c.sheet, c.cell);
    }
    CHECK(seen.size() == 8);
}

TEST_CASE("the map keeps ground paths and the ends of raised ones, and their styles in the order met")
{
    const save::model::TerrainGrid g = flat_grid(4, 3);
    std::vector<save::model::PathTile> paths { ground_tile(0, 0), ground_tile(1, 0, "Brick"), ground_tile(2, 2), ground_tile(9, 9), ground_tile(3, 0, "") };
    save::model::PathTile raised = ground_tile(1, 1);
    raised.kind = save::model::PathKind::Flying;
    raised.elevation = save::model::PathElevation { };
    paths.push_back(raised);

    const GroundPathMap map = ground_path_map(paths, g);
    REQUIRE(map.styles == std::vector<std::string> { "Gravel", "Brick" });
    CHECK(has_ground_path(map, { 0, 0 }));
    CHECK(has_ground_path(map, { 1, 0 }));
    CHECK(has_ground_path(map, { 2, 2 }));
    CHECK_FALSE(has_ground_path(map, { 1, 1 }));
    CHECK_FALSE(has_ground_path(map, { 3, 0 }));
    CHECK_FALSE(has_ground_path(map, { -1, 0 }));
    CHECK_FALSE(has_ground_path(map, { 4, 0 }));
    CHECK(map.tiles[1] == 1);
    REQUIRE(map.raised.contains(1 * 4 + 1));
}

TEST_CASE("a lone path tile is drawn whole, closed all round")
{
    const save::model::TerrainGrid g = flat_grid(3, 3, 2);
    const std::vector<save::model::PathTile> paths { ground_tile(1, 1) };
    const TerrainMesh m = ground_path_mesh(ground_path_map(paths, g), g);
    REQUIRE(m.batches.size() == 1);
    CHECK(m.batches[0].paint == (path_cell(PathLook::Closed).sheet == 0 ? GroundPaint::PathA : GroundPaint::PathB));
    CHECK(m.batches[0].count == 6);
    for (const GroundVertex& v : m.vertices)
        CHECK(v.position.y == doctest::Approx(2 + kPathLift));
}

TEST_CASE("a straight run along x shows edges along z, and its ends split into quarters")
{
    const save::model::TerrainGrid g = flat_grid(5, 3);
    const std::vector<save::model::PathTile> paths { ground_tile(1, 1), ground_tile(2, 1), ground_tile(3, 1) };
    const TerrainMesh m = ground_path_mesh(ground_path_map(paths, g), g);
    // The middle tile is one quad; each end has two quarters closed and two edged.
    CHECK(m.vertices.size() == 6 + 2 * 4 * 6);
    CHECK(vertices_of(m, GroundPaint::PathA) + vertices_of(m, GroundPaint::PathB) == m.vertices.size());
}

TEST_CASE("the path follows the ground's slope")
{
    save::model::TerrainGrid g = flat_grid(2, 1);
    g.cells[0].corners = { 0, 4, 0, 4 }; // Rising along x
    g.cells[1].corners = { 4, 4, 4, 4 };
    const std::vector<save::model::PathTile> paths { ground_tile(0, 0), ground_tile(1, 0) };
    const TerrainMesh m = ground_path_mesh(ground_path_map(paths, g), g);
    for (const GroundVertex& v : m.vertices)
    {
        if (v.position.x <= g.world_x(1))
            CHECK(v.position.y == doctest::Approx((v.position.x - g.world_x(0)) / (g.world_x(1) - g.world_x(0)) * 4 + kPathLift));
    }
}

TEST_CASE("a map for another grid draws nothing")
{
    const save::model::TerrainGrid g = flat_grid(3, 3);
    const std::vector<save::model::PathTile> paths { ground_tile(1, 1) };
    const GroundPathMap map = ground_path_map(paths, g);
    CHECK(ground_path_mesh(map, flat_grid(4, 4)).vertices.empty());
}

TEST_CASE("a ground path runs on into the foot of a raised path that meets the ground")
{
    save::model::TerrainGrid g = flat_grid(4, 1, 3);
    save::model::PathTile slope = ground_tile(1, 0);
    slope.kind = save::model::PathKind::Flying;
    slope.elevation = save::model::PathElevation { };
    slope.elevation->slope = 4; // Gentle, low end on the ground
    slope.elevation->quantised_height = 3;
    save::model::PathTile bridge = ground_tile(3, 0);
    bridge.kind = save::model::PathKind::Flying;
    bridge.elevation = save::model::PathElevation { };
    bridge.elevation->quantised_height = 9; // Flat, high above
    const std::vector<save::model::PathTile> paths { ground_tile(0, 0), slope, ground_tile(2, 0), bridge };
    const GroundPathMap map = ground_path_map(paths, g);
    CHECK(path_continues(map, g, { 0, 0 }, 1, 0));
    CHECK(path_continues(map, g, { 2, 0 }, -1, 0));
    CHECK_FALSE(path_continues(map, g, { 2, 0 }, 1, 0));
    CHECK_FALSE(path_continues(map, g, { 0, 0 }, -1, 0));
    CHECK(path_rise(1) == 2);
    CHECK(path_rise(5) == 1);
    CHECK(path_rise(0) == 0);
}

TEST_CASE("a mesh cut down to some tiles keeps their triangles in their batches")
{
    const save::model::TerrainGrid g = flat_grid(5, 3);
    const std::vector<save::model::PathTile> paths { ground_tile(1, 1), ground_tile(2, 1), ground_tile(3, 1) };
    const TerrainMesh m = ground_path_mesh(ground_path_map(paths, g), g);
    std::vector<bool> keep(15, false);
    keep[1 * 5 + 2] = true; // The middle tile only
    const TerrainMesh middle = only_tiles(m, g, keep);
    CHECK(middle.vertices.size() == 6);
    CHECK(middle.indices.size() == 6);
    REQUIRE(middle.batches.size() == 1);
    CHECK(middle.batches[0].first == 0);
    keep.assign(15, true);
    CHECK(only_tiles(m, g, keep).vertices.size() == m.vertices.size());
}

namespace
{
    save::model::Ride flat_ride(int col, int row, std::uint8_t turns, std::int32_t height = 0)
    {
        save::model::Ride ride;
        ride.kind = save::model::RideKind::Flat;
        ride.base.spot = save::model::PlacementSpot { 7, col, row, height, turns };
        return ride;
    }

    /// Two tiles along x from the spot.
    std::vector<save::model::Tile> two_wide(const save::model::PlacementSpot& spot)
    {
        return { { spot.col, spot.row }, { spot.col + 1, spot.row } };
    }

    std::vector<save::model::Tile> nothing(const save::model::PlacementSpot&)
    {
        return { };
    }
} // namespace

TEST_CASE("a door paves its tile, opening onto the ride in front and the path behind")
{
    save::model::RideModel rides;
    save::model::Ride& coaster = rides.rides.emplace_back();
    coaster.kind = save::model::RideKind::Tracked;
    save::model::Station& station = coaster.stations.emplace_back();
    station.entrance_spot = save::model::PlacementSpot { 8, 3, 3, 2, 0 }; // Facing -X
    station.exit_spot = save::model::PlacementSpot { 9, 5, 5, 0, 1 };     // Facing +Z
    const std::vector<Paving> pavings = ride_pavings(rides, two_wide, nothing);
    REQUIRE(pavings.size() == 2);
    CHECK(pavings[0].tiles == std::vector { save::model::Tile { 3, 3 } });
    CHECK(pavings[0].height == 2);
    CHECK(pavings[0].entrance);
    CHECK(pavings[0].source == save::model::Tile { 4, 3 });
    CHECK(pavings[0].links == std::vector { save::model::Tile { 2, 3 }, save::model::Tile { 4, 3 } });
    CHECK_FALSE(pavings[1].entrance);
    CHECK(pavings[1].source == save::model::Tile { 5, 4 });
    // Each door's columns take the style of its paving.
    CHECK(pavings[0].owner == 8);
    CHECK(pavings[1].owner == 9);

    coaster.base.demolished = true;
    CHECK(ride_pavings(rides, two_wide, nothing).empty());
}

TEST_CASE("a flat ride paves its footprint, opening onto its doors and looking like the path past its exit")
{
    save::model::RideModel rides;
    save::model::Ride& ride = rides.rides.emplace_back(flat_ride(2, 2, 0));
    save::model::Station& station = ride.stations.emplace_back();
    station.entrance_spot = save::model::PlacementSpot { 8, 1, 2, 0, 2 }; // Facing +X, into the ride
    station.exit_spot = save::model::PlacementSpot { 9, 2, 3, 0, 3 };     // Facing -Z
    const std::vector<Paving> pavings = ride_pavings(rides, two_wide, nothing);
    REQUIRE(pavings.size() == 3);
    const Paving& floor = pavings[2];
    CHECK(floor.tiles == std::vector { save::model::Tile { 2, 2 }, save::model::Tile { 3, 2 } });
    CHECK(floor.source == save::model::Tile { 2, 4 });
    CHECK(floor.links == std::vector { save::model::Tile { 2, 3 }, save::model::Tile { 1, 2 } });
    CHECK(floor.owner == 7);

    // Nothing when the footprint is unknown.
    CHECK(ride_pavings(rides, nothing, nothing).size() == 2);
}

TEST_CASE("a tracked ride's base paves its floor, opening onto its doors, and its other pieces pave nothing")
{
    save::model::RideModel rides;
    save::model::Ride& tower = rides.rides.emplace_back();
    tower.kind = save::model::RideKind::Tracked;
    save::model::Track& track = tower.track.emplace();
    track.pieces.push_back({ .scenery = 20, .tile_x = 2, .tile_z = 2, .height = 1 });  // The base
    track.pieces.push_back({ .scenery = 21, .tile_x = 2, .tile_z = 2, .height = 10 }); // A section above it
    track.pieces.push_back({ .tile_x = 2, .tile_z = 2, .height = 20 });                // No object of its own
    save::model::Station& station = tower.stations.emplace_back();
    station.entrance_spot = save::model::PlacementSpot { 8, 1, 2, 1, 2 }; // Facing +X, into the base
    station.exit_spot = save::model::PlacementSpot { 9, 2, 3, 1, 3 };     // Facing -Z
    const auto base_only = [](const save::model::PlacementSpot& spot) { return spot.object == 20 ? two_wide(spot) : std::vector<save::model::Tile> { }; };
    const std::vector<Paving> pavings = ride_pavings(rides, nothing, base_only);
    REQUIRE(pavings.size() == 3);
    const Paving& floor = pavings[2];
    CHECK(floor.tiles == std::vector { save::model::Tile { 2, 2 }, save::model::Tile { 3, 2 } });
    CHECK(floor.height == 1);
    CHECK(floor.source == save::model::Tile { 2, 4 });
    CHECK(floor.links == std::vector { save::model::Tile { 2, 3 }, save::model::Tile { 1, 2 } });
}

TEST_CASE("a stall paves its footprint and opens onto the side it faces")
{
    save::model::RideModel rides;
    save::model::Stall& stall = rides.stalls.emplace_back();
    stall.base.spot = save::model::PlacementSpot { 7, 4, 4, 0, 0 };
    for (const auto& [facing, front] : { std::pair { 0, std::vector { save::model::Tile { 4, 3 }, save::model::Tile { 5, 3 } } }, { 1, { save::model::Tile { 3, 4 } } }, { 2, { save::model::Tile { 4, 5 }, save::model::Tile { 5, 5 } } }, { 3, { save::model::Tile { 6, 4 } } } })
    {
        stall.base.facing = static_cast<std::uint8_t>(facing);
        const std::vector<Paving> pavings = ride_pavings(rides, two_wide, nothing);
        REQUIRE(pavings.size() == 1);
        CHECK(pavings[0].tiles.size() == 2);
        CHECK(pavings[0].links == front);
        CHECK(pavings[0].source == front.front());
    }
}

TEST_CASE("a facility without doors paves its footprint, open on every side, in the plain style")
{
    save::model::RideModel rides;
    save::model::Ride& toilet = rides.rides.emplace_back();
    toilet.kind = save::model::RideKind::Special;
    toilet.base.spot = save::model::PlacementSpot { 7, 4, 4, 0, 0 };
    const std::vector<Paving> pavings = ride_pavings(rides, two_wide, nothing);
    REQUIRE(pavings.size() == 1);
    CHECK_FALSE(pavings[0].source);
    CHECK(pavings[0].links.size() == 6);
}

TEST_CASE("paving looks like the path at its source; only an entrance takes a queue, straight in; else the plain style")
{
    const save::model::TerrainGrid g = flat_grid(6, 6);
    Paving door { .tiles = { { 2, 2 } }, .height = 0, .source = save::model::Tile { 3, 2 }, .links = { { 1, 2 }, { 3, 2 } } };

    std::vector<PavedTile> laid = lay_pavings({ }, std::span(&door, 1), g);
    REQUIRE(laid.size() == 1);
    CHECK(laid[0].path.surface_name == kPavingStyle);
    CHECK_FALSE(laid[0].path.elevation);

    const std::array marble { ground_tile(3, 2, "Marble") };
    laid = lay_pavings(marble, std::span(&door, 1), g);
    CHECK(laid[0].path.surface_name == "Marble");
    CHECK(laid[0].path.col == 2);

    save::model::PathTile queue = ground_tile(3, 2, "QueueRed");
    queue.kind = save::model::PathKind::Queue;
    queue.queue = save::model::PathQueueInfo { .start_direction = 2, .end_direction = 3 };
    queue.elevation = save::model::PathElevation { .slope = save::model::kSlopeOnGround };
    const std::array queues { queue };
    laid = lay_pavings(queues, std::span(&door, 1), g);
    CHECK(laid[0].path.surface_name == kPavingStyle);
    door.entrance = true;
    laid = lay_pavings(queues, std::span(&door, 1), g);
    CHECK(laid[0].path.surface_name == "QueueRed");
    REQUIRE(laid[0].path.queue);
    CHECK(laid[0].path.queue->start_direction == 0); // Heading on towards lower columns
    CHECK(laid[0].path.queue->end_direction == 0);

    // A path far off the paving's height does not count.
    door.height = 4;
    laid = lay_pavings(marble, std::span(&door, 1), g);
    CHECK(laid[0].path.surface_name == kPavingStyle);
}

TEST_CASE("paving off level ground is raised at its height, and tiles holding a path keep it")
{
    save::model::TerrainGrid g = flat_grid(6, 6);
    g.cells[static_cast<std::size_t>(2 * 6 + 3)].corners = { 0, 1, 0, 1 };
    const Paving stall { .tiles = { { 2, 2 }, { 3, 2 }, { 4, 2 }, { 9, 9 } }, .height = 0 };
    const std::array paths { ground_tile(4, 2) };
    const std::vector<PavedTile> laid = lay_pavings(paths, std::span(&stall, 1), g);
    REQUIRE(laid.size() == 2);
    CHECK_FALSE(laid[0].path.elevation);
    REQUIRE(laid[1].path.elevation);
    CHECK(laid[1].path.elevation->quantised_height == 0);
    CHECK(laid[1].path.elevation->slope == 0);
}

TEST_CASE("paving joins only itself and its links, never a path beside it, fence or not")
{
    const save::model::TerrainGrid g = flat_grid(6, 6);
    // A two-tile floor at row 2 with its door at (1, 2); a park path runs past along row 3 and on to the door.
    const std::array pavings { Paving { .tiles = { { 2, 2 }, { 3, 2 } }, .links = { { 1, 2 } } }, Paving { .tiles = { { 1, 2 } }, .source = save::model::Tile { 0, 2 }, .links = { { 2, 2 }, { 0, 2 } } } };
    std::vector<save::model::PathTile> paths { ground_tile(0, 2), ground_tile(1, 3), ground_tile(2, 3), ground_tile(3, 3) };
    const std::vector<PavedTile> laid = lay_pavings(paths, pavings, g);
    REQUIRE(laid.size() == 3);
    for (const PavedTile& t : laid)
        paths.push_back(t.path);
    GroundPathMap map = ground_path_map(paths, g);
    seal_pavings(map, laid, pavings);

    CHECK(path_continues(map, g, { 2, 2 }, 1, 0));       // Within the floor
    CHECK(path_continues(map, g, { 2, 2 }, -1, 0));      // Floor into its door
    CHECK(path_continues(map, g, { 1, 2 }, -1, 0));      // Door onto the path behind it
    CHECK(path_continues(map, g, { 0, 2 }, 1, 0));       // and back
    CHECK_FALSE(path_continues(map, g, { 2, 2 }, 0, 1)); // Floor and the path running past
    CHECK_FALSE(path_continues(map, g, { 2, 3 }, 0, -1));
    CHECK_FALSE(path_continues(map, g, { 1, 2 }, 0, 1)); // Door and the path beside it
    CHECK(may_join(map, { 1, 3 }, { 2, 3 }));

    // The path past draws its edge along the floor: closed on the floor's side all the way.
    const TerrainMesh alone = ground_path_mesh(ground_path_map(std::array { ground_tile(1, 3), ground_tile(2, 3), ground_tile(3, 3) }, g), g);
    const TerrainMesh beside = ground_path_mesh(map, g);
    CHECK(vertices_of(beside, GroundPaint::PathA) + vertices_of(beside, GroundPaint::PathB) > vertices_of(alone, GroundPaint::PathA) + vertices_of(alone, GroundPaint::PathB));
}
