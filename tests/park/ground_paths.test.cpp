// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_paths.hpp"

#include <doctest/doctest.h>
#include <set>

using namespace rct3;
using namespace rct3::park;

namespace
{

    save::TerrainGrid flat_grid(int width, int height, float level = 0)
    {
        save::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.origin_x = -6;
        g.origin_z = 2;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(width * height));
        for (save::TerrainCell& c : g.cells)
            c.corners = { level, level, level, level };
        return g;
    }

    save::PathTile ground(int col, int row, std::string style = "Gravel")
    {
        save::PathTile p;
        p.kind = save::PathKind::Ground;
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
    CHECK(quarter_look(true, true, true) == PathLook::Open);
    CHECK(quarter_look(true, true, false) == PathLook::Notched);
    CHECK(quarter_look(false, true, false) == PathLook::EdgeX);
    CHECK(quarter_look(false, true, true) == PathLook::EdgeXJoined);
    CHECK(quarter_look(true, false, false) == PathLook::EdgeZ);
    CHECK(quarter_look(true, false, true) == PathLook::EdgeZJoined);
    CHECK(quarter_look(false, false, false) == PathLook::Closed);
    CHECK(quarter_look(false, false, true) == PathLook::Closed);
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
    const save::TerrainGrid g = flat_grid(4, 3);
    std::vector<save::PathTile> paths { ground(0, 0), ground(1, 0, "Brick"), ground(2, 2), ground(9, 9), ground(3, 0, "") };
    save::PathTile raised = ground(1, 1);
    raised.kind = save::PathKind::Flying;
    raised.elevation = save::PathElevation { };
    paths.push_back(raised);

    const GroundPathMap map = ground_path_map(paths, g);
    REQUIRE(map.styles == std::vector<std::string> { "Gravel", "Brick" });
    CHECK(has_ground_path(map, 0, 0));
    CHECK(has_ground_path(map, 1, 0));
    CHECK(has_ground_path(map, 2, 2));
    CHECK_FALSE(has_ground_path(map, 1, 1));
    CHECK_FALSE(has_ground_path(map, 3, 0));
    CHECK_FALSE(has_ground_path(map, -1, 0));
    CHECK_FALSE(has_ground_path(map, 4, 0));
    CHECK(map.tiles[1] == 1);
    REQUIRE(map.raised.contains(1 * 4 + 1));
}

TEST_CASE("a lone path tile is drawn whole, closed all round")
{
    const save::TerrainGrid g = flat_grid(3, 3, 2);
    const std::vector<save::PathTile> paths { ground(1, 1) };
    const TerrainMesh m = ground_path_mesh(ground_path_map(paths, g), g);
    REQUIRE(m.batches.size() == 1);
    CHECK(m.batches[0].paint == (path_cell(PathLook::Closed).sheet == 0 ? GroundPaint::PathA : GroundPaint::PathB));
    CHECK(m.batches[0].count == 6);
    for (const GroundVertex& v : m.vertices)
        CHECK(v.position.y == doctest::Approx(2 + kPathLift));
}

TEST_CASE("a straight run along x shows edges along z, and its ends split into quarters")
{
    const save::TerrainGrid g = flat_grid(5, 3);
    const std::vector<save::PathTile> paths { ground(1, 1), ground(2, 1), ground(3, 1) };
    const TerrainMesh m = ground_path_mesh(ground_path_map(paths, g), g);
    // The middle tile is one quad; each end has two quarters closed and two edged.
    CHECK(m.vertices.size() == 6 + 2 * 4 * 6);
    CHECK(vertices_of(m, GroundPaint::PathA) + vertices_of(m, GroundPaint::PathB) == m.vertices.size());
}

TEST_CASE("the path follows the ground's slope")
{
    save::TerrainGrid g = flat_grid(2, 1);
    g.cells[0].corners = { 0, 4, 0, 4 }; // Rising along x
    g.cells[1].corners = { 4, 4, 4, 4 };
    const std::vector<save::PathTile> paths { ground(0, 0), ground(1, 0) };
    const TerrainMesh m = ground_path_mesh(ground_path_map(paths, g), g);
    for (const GroundVertex& v : m.vertices)
    {
        if (v.position.x <= g.world_x(1))
            CHECK(v.position.y == doctest::Approx((v.position.x - g.world_x(0)) / (g.world_x(1) - g.world_x(0)) * 4 + kPathLift));
    }
}

TEST_CASE("a map for another grid draws nothing")
{
    const save::TerrainGrid g = flat_grid(3, 3);
    const std::vector<save::PathTile> paths { ground(1, 1) };
    const GroundPathMap map = ground_path_map(paths, g);
    CHECK(ground_path_mesh(map, flat_grid(4, 4)).vertices.empty());
}

TEST_CASE("a ground path runs on into the foot of a raised path that meets the ground")
{
    save::TerrainGrid g = flat_grid(4, 1, 3);
    save::PathTile slope = ground(1, 0);
    slope.kind = save::PathKind::Flying;
    slope.elevation = save::PathElevation { };
    slope.elevation->slope = 4; // Gentle, low end on the ground
    slope.elevation->quantised_height = 3;
    save::PathTile bridge = ground(3, 0);
    bridge.kind = save::PathKind::Flying;
    bridge.elevation = save::PathElevation { };
    bridge.elevation->quantised_height = 9; // Flat, high above
    const std::vector<save::PathTile> paths { ground(0, 0), slope, ground(2, 0), bridge };
    const GroundPathMap map = ground_path_map(paths, g);
    CHECK(path_continues(map, g, 0, 0, 1, 0));
    CHECK(path_continues(map, g, 2, 0, -1, 0));
    CHECK_FALSE(path_continues(map, g, 2, 0, 1, 0));
    CHECK_FALSE(path_continues(map, g, 0, 0, -1, 0));
    CHECK(path_rise(1) == 2);
    CHECK(path_rise(5) == 1);
    CHECK(path_rise(0) == 0);
}

TEST_CASE("a mesh cut down to some tiles keeps their triangles in their batches")
{
    const save::TerrainGrid g = flat_grid(5, 3);
    const std::vector<save::PathTile> paths { ground(1, 1), ground(2, 1), ground(3, 1) };
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
    save::Ride flat_ride(int col, int row, std::uint8_t turns, std::int32_t height = 0)
    {
        save::Ride ride;
        ride.kind = save::RideKind::Flat;
        ride.base.spot = save::PlacementSpot { 7, col, row, height, turns };
        return ride;
    }

    std::optional<std::array<std::int32_t, 2>> two_by_three(std::uint64_t)
    {
        return std::array<std::int32_t, 2> { 2, 3 };
    }
} // namespace

TEST_CASE("a flat ride's floor covers its footprint, turned with it")
{
    const save::TerrainGrid g = flat_grid(10, 10);
    const std::array rides { flat_ride(2, 2, 0), flat_ride(2, 2, 1) };
    const std::vector<RideFloor> floors = ride_floors(rides, two_by_three, g);
    REQUIRE(floors.size() == 2);
    CHECK(floors[0].col1 - floors[0].col0 == 1);
    CHECK(floors[0].row1 - floors[0].row0 == 2);
    CHECK(floors[1].col1 - floors[1].col0 == 2);
    CHECK(floors[1].row1 - floors[1].row0 == 1);
    CHECK(floors[0].col0 == 2);
    CHECK(floors[0].row0 == 2);
}

TEST_CASE("a ride off the ground has no floor, nor one whose model is unknown")
{
    const save::TerrainGrid g = flat_grid(10, 10);
    const std::array rides { flat_ride(2, 2, 0, 3) };
    CHECK(ride_floors(rides, two_by_three, g).empty());
    const std::array level { flat_ride(2, 2, 0) };
    CHECK(ride_floors(level, [](std::uint64_t) { return std::optional<std::array<std::int32_t, 2>> { }; }, g).empty());
}

TEST_CASE("a floor is paved like the path its exit lets out onto, else plain")
{
    const save::TerrainGrid g = flat_grid(10, 10);
    RideFloor floor { 2, 2, 3, 4, std::array { 1, 3 } };
    GroundPathMap map = ground_path_map({ }, g);
    pave_ride_floors(map, std::span(&floor, 1));
    REQUIRE(map.styles.size() == 1);
    CHECK(map.styles[0] == kRideFloorStyle);
    CHECK(has_ground_path(map, 3, 4));
    CHECK_FALSE(has_ground_path(map, 4, 4));

    const std::array paths { ground(1, 3, "Marble") };
    map = ground_path_map(paths, g);
    pave_ride_floors(map, std::span(&floor, 1));
    CHECK(map.styles == std::vector<std::string> { "Marble" });
    CHECK(map.tiles[static_cast<std::size_t>(2 * map.width + 2)] == 0);
}
