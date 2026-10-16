// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/footpath_builder.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    /// A small flat park, every tile owned unless told otherwise.
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

TEST_CASE("each heading steps one tile, and turning right goes clockwise")
{
    const PathSpot from { 5, 5 };
    CHECK(step(from, 0) == PathSpot { 5, 6 });
    CHECK(step(from, 1) == PathSpot { 6, 5 });
    CHECK(step(from, 2) == PathSpot { 5, 4 });
    CHECK(step(from, 3) == PathSpot { 4, 5 });
    CHECK(step(from, 4) == step(from, 0));

    FootpathBuilder b;
    CHECK(press(b, PathControl::TurnRight) == PathStep::None);
    CHECK(b.heading == 1);
    CHECK(press(b, PathControl::TurnLeft) == PathStep::None);
    CHECK(press(b, PathControl::TurnLeft) == PathStep::None);
    CHECK(b.heading == 3);
}

TEST_CASE("going straight lays, and so does picking the slope already chosen")
{
    FootpathBuilder b;
    CHECK(press(b, PathControl::Straight) == PathStep::Lay);
    CHECK(press(b, PathControl::Flat) == PathStep::Lay);
    CHECK(press(b, PathControl::Up) == PathStep::None);
    CHECK(b.slope == PathSlope::Up);
    CHECK(press(b, PathControl::Up) == PathStep::Lay);
    CHECK(press(b, PathControl::Remove) == PathStep::Remove);
    CHECK_FALSE(b.cut_into_ground);
    CHECK(press(b, PathControl::CutIntoGround) == PathStep::None);
    CHECK(b.cut_into_ground);
}

TEST_CASE("no turning while the last piece slopes")
{
    FootpathBuilder b;
    b.on_slope = true;
    CHECK_FALSE(can_turn(b));
    (void)press(b, PathControl::TurnRight);
    CHECK(b.heading == 0);
}

TEST_CASE("slope controls map to slopes and back")
{
    for (std::uint8_t i = 0; i < kPathSlopeCount; ++i)
    {
        const auto slope = static_cast<PathSlope>(i);
        CHECK(picks_slope(slope_control(slope)));
        CHECK(control_slope(slope_control(slope)) == slope);
    }
    CHECK_FALSE(picks_slope(PathControl::Straight));
    CHECK(control_slope(PathControl::Remove) == PathSlope::Flat);
    CHECK(stored_slope(PathSlope::Flat) == 0);
    CHECK(slope_rise(PathSlope::Flat) == 0.0f);
    CHECK(slope_rise(PathSlope::Up) > 0.0f);
    CHECK(slope_rise(PathSlope::Down) == -slope_rise(PathSlope::Up));
    CHECK(slope_rise(PathSlope::SteepUp) > slope_rise(PathSlope::Up));
}

TEST_CASE("what stops the next piece")
{
    save::TerrainGrid g = owned_grid(4, 4);
    FootpathBuilder b;
    CHECK(lay_problem(b, g, false) == LayProblem::NoStart);
    CHECK(lay_problem(b, g, true) == LayProblem::Queue);
    start_at(b, { 1, 3 });
    CHECK(lay_problem(b, g, false) == LayProblem::None);
    b.slope = PathSlope::Down;
    CHECK(lay_problem(b, g, false) == LayProblem::Raised);
    b.slope = PathSlope::Flat;
    g.cells[3 * 4 + 1].flags = 0;
    CHECK(lay_problem(b, g, false) == LayProblem::NotOwned);
    g.cells[3 * 4 + 1].flags = static_cast<std::uint8_t>(save::TileFlag::Owned);
    std::vector<save::PathTile> paths;
    CHECK(lay_ground_path(b, paths, "Gravel", 1));
    // Heading 0 leaves the four-row park from row 3.
    CHECK(lay_problem(b, g, false) == LayProblem::OffMap);
}

TEST_CASE("laying walks on, over paths already there, and removing steps back")
{
    FootpathBuilder b;
    std::vector<save::PathTile> paths { ground_path({ 2, 1 }, "Old", 7) };
    start_at(b, { 2, 0 });
    CHECK(next_spot(b) == PathSpot { 2, 0 });
    CHECK(lay_ground_path(b, paths, "Gravel", 10));
    CHECK(paths.size() == 2);
    CHECK(paths.back().surface_name == "Gravel");
    CHECK(paths.back().kind == save::PathKind::Ground);
    CHECK(paths.back().id == 10);
    // The next tile has a path: walked over, nothing added.
    CHECK_FALSE(lay_ground_path(b, paths, "Gravel", 11));
    CHECK(paths.size() == 2);
    CHECK(b.trail.size() == 2);
    CHECK(next_spot(b) == PathSpot { 2, 2 });

    // Stepping back over the walked tile keeps the old path.
    CHECK_FALSE(remove_last(b, paths));
    CHECK(paths.size() == 2);
    CHECK(remove_last(b, paths));
    CHECK(paths.size() == 1);
    CHECK(paths.front().id == 7);
    CHECK(b.trail.empty());
    CHECK(next_spot(b) == PathSpot { 2, 0 });
    CHECK_FALSE(remove_last(b, paths));
}

TEST_CASE("a raised piece costs more for each started unit of height")
{
    CHECK(piece_cost(1200, false, 5.0f) == 1200);
    CHECK(piece_cost(1200, true, 0.0f) == 1200);
    CHECK(piece_cost(1200, true, 0.25f) == 1200 + kRaisedPieceCost);
    CHECK(piece_cost(1200, true, 1.0f) == 1200 + kRaisedPieceCost);
    CHECK(piece_cost(1200, true, 2.5f) == 1200 + 3 * kRaisedPieceCost);
}
