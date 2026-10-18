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
    // Down from the ground goes into it, unless cutting.
    b.slope = PathSlope::Down;
    CHECK(lay_problem(b, g, false) == LayProblem::Underground);
    b.cut_into_ground = true;
    CHECK(lay_problem(b, g, false) == LayProblem::None);
    b.cut_into_ground = false;
    b.slope = PathSlope::Flat;
    g.cells[3 * 4 + 1].flags = 0;
    CHECK(lay_problem(b, g, false) == LayProblem::NotOwned);
    g.cells[3 * 4 + 1].flags = static_cast<std::uint8_t>(save::TileFlag::Owned);
    std::vector<save::PathTile> paths;
    CHECK(lay_path(b, paths, *next_piece(b, g), "Gravel", 1, 0));
    // Heading 0 leaves the four-row park from row 3.
    CHECK(lay_problem(b, g, false) == LayProblem::OffMap);
}

TEST_CASE("laying walks on, over paths already there, and removing steps back")
{
    const save::TerrainGrid g = owned_grid(4, 4);
    FootpathBuilder b;
    std::vector<save::PathTile> paths { ground_path({ 2, 1 }, "Old", 7) };
    start_at(b, { 2, 0 });
    CHECK(next_spot(b) == PathSpot { 2, 0 });
    CHECK(lay_path(b, paths, *next_piece(b, g), "Gravel", 10, 0));
    CHECK(paths.size() == 2);
    CHECK(paths.back().surface_name == "Gravel");
    CHECK(paths.back().kind == save::PathKind::Ground);
    CHECK(paths.back().id == 10);
    // The next tile has a path: walked over, nothing added.
    CHECK_FALSE(lay_path(b, paths, *next_piece(b, g), "Gravel", 11, 0));
    CHECK(paths.size() == 2);
    CHECK(b.trail.size() == 2);
    CHECK(next_spot(b) == PathSpot { 2, 2 });

    // Stepping back over the walked tile keeps the old path.
    CHECK_FALSE(remove_last(b, paths));
    CHECK(paths.size() == 2);
    CHECK(remove_last(b, paths)->id == 10);
    CHECK(paths.size() == 1);
    CHECK(paths.front().id == 7);
    CHECK(b.trail.empty());
    CHECK(next_spot(b) == PathSpot { 2, 0 });
    CHECK_FALSE(remove_last(b, paths));
}

TEST_CASE("slopes climb off the ground and later pieces start where the last ended")
{
    const save::TerrainGrid g = owned_grid(6, 6);
    FootpathBuilder b;
    std::vector<save::PathTile> paths;
    start_at(b, { 1, 1 });
    b.heading = 1;
    b.slope = PathSlope::Up;
    const NextPiece up = *next_piece(b, g);
    CHECK(up == NextPiece { { 1, 1 }, 0, 1, false, false });
    CHECK(lay_path(b, paths, up, "Gravel", 10, 20));
    const save::PathTile& laid = paths.back();
    CHECK(laid.kind == save::PathKind::Flying);
    CHECK(laid.elevation->slope == 4);
    CHECK(laid.elevation->quantised_height == 0);
    CHECK(laid.elevation->scenery_ref == 20);
    CHECK(laid.direction == 1);
    CHECK_FALSE(can_turn(b));

    b.slope = PathSlope::Flat;
    const NextPiece flat = *next_piece(b, g);
    CHECK(flat.start == 1);
    CHECK_FALSE(flat.ground);
    CHECK(height_above_ground(flat, g) == doctest::Approx(1));
    CHECK(lay_path(b, paths, flat, "Gravel", 11, 21));
    CHECK(paths.back().elevation->slope == 0);
    CHECK(can_turn(b));

    // Back down to the ground, then on along it.
    b.slope = PathSlope::Down;
    CHECK(lay_path(b, paths, *next_piece(b, g), "Gravel", 12, 22));
    CHECK(paths.back().elevation->quantised_height == 1);
    b.slope = PathSlope::Flat;
    CHECK(next_piece(b, g)->ground);
}

TEST_CASE("a hill stops a piece, or is cut down to it")
{
    save::TerrainGrid g = owned_grid(4, 4);
    for (float& c : g.cells[1 * 4 + 2].corners)
        c = 3;
    FootpathBuilder b;
    start_at(b, { 1, 1 });
    b.heading = 1;
    std::vector<save::PathTile> paths;
    CHECK(lay_path(b, paths, *next_piece(b, g), "Gravel", 10, 0));
    CHECK(next_piece(b, g)->cut);
    CHECK(lay_problem(b, g, false) == LayProblem::Underground);
    b.cut_into_ground = true;
    const NextPiece cut = *next_piece(b, g);
    CHECK(lay_problem(b, g, false) == LayProblem::None);
    CHECK(cut.ground);
    cut_down(g, cut, b.heading);
    CHECK(g.at(2, 1).max_height() == 0);

    // A piece picked on a tile that is not flat starts above it.
    save::TerrainGrid rough = owned_grid(4, 4);
    rough.cells[0].corners = { 0, 1, 0, 2 };
    FootpathBuilder c;
    start_at(c, { 0, 0 });
    CHECK(next_piece(c, rough)->start == 2);
    CHECK_FALSE(next_piece(c, rough)->ground);
}

TEST_CASE("a raised piece costs more for each started unit of height")
{
    CHECK(piece_cost(1200, false, 5.0f) == 1200);
    CHECK(piece_cost(1200, true, 0.0f) == 1200);
    CHECK(piece_cost(1200, true, 0.25f) == 1200 + kRaisedPieceCost);
    CHECK(piece_cost(1200, true, 1.0f) == 1200 + kRaisedPieceCost);
    CHECK(piece_cost(1200, true, 2.5f) == 1200 + 3 * kRaisedPieceCost);
}
