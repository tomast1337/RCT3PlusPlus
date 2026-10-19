// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_shapes.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    /// Flat at height 2, `size` tiles each way.
    save::TerrainGrid flat(int size)
    {
        save::TerrainGrid g;
        g.width = size;
        g.height = size;
        g.cells.resize(static_cast<std::size_t>(size * size));
        for (save::TerrainCell& c : g.cells)
            c.corners = { 2, 2, 2, 2 };
        return g;
    }

    float point(const save::TerrainGrid& g, int c, int r)
    {
        return g.at(c, r).corners[0]; // The tile's own first corner is the point (c, r)
    }

    const auto anywhere = [](int, int) { return true; };
} // namespace

TEST_CASE("each shape rises or sinks most where it should and not past its reach")
{
    for (const GroundShape up : { GroundShape::Hill, GroundShape::Mountain, GroundShape::Mesa })
    {
        CHECK(shape_share(up, 0) == doctest::Approx(1));
        CHECK(shape_share(up, 0.5f) > 0);
        CHECK(shape_share(up, 1) == doctest::Approx(0));
    }
    CHECK(shape_share(GroundShape::Mountain, 0.5f) < shape_share(GroundShape::Hill, 0.5f)); // Sharper
    CHECK(shape_share(GroundShape::Mesa, 0.5f) == doctest::Approx(1));                      // Flat on top
    CHECK(shape_share(GroundShape::Ridge, 0, 0.8f) > 0);                                    // Long along its line
    CHECK(shape_share(GroundShape::Ridge, 0.5f, 0) == doctest::Approx(0));                  // Narrow across
    CHECK(shape_share(GroundShape::Canyon, 0.5f) == doctest::Approx(-1));                   // Flat floor
    CHECK(shape_share(GroundShape::Trough, 0) == doctest::Approx(-1));
    CHECK(shape_share(GroundShape::Crater, 0) == doctest::Approx(-1));
    CHECK(shape_share(GroundShape::Crater, 0.8f) > 0); // The rim
    CHECK(shape_share(GroundShape::Crater, 1.2f) == 0);
}

TEST_CASE("a hill raises the corner points round its middle, shared by the tiles, from the stroke's start")
{
    save::TerrainGrid g = flat(10);
    const CornerHeights before = corner_heights(g, anywhere);
    const float moved = shape_ground(g, before, GroundShape::Hill, { 5, 5 }, 3, { 1, 0 }, 4);
    CHECK(moved > 0);
    CHECK(point(g, 5, 5) == doctest::Approx(6));
    CHECK(g.at(4, 4).corners[3] == doctest::Approx(6)); // The same point, as the tile up and left holds it
    CHECK(point(g, 6, 5) > 2);
    CHECK(point(g, 6, 5) < 6);
    CHECK(point(g, 9, 5) == doctest::Approx(2)); // Past the reach

    // A smaller stroke from the same start replaces the bigger one, as the pointer moving back does.
    shape_ground(g, before, GroundShape::Hill, { 5, 5 }, 3, { 1, 0 }, 1);
    CHECK(point(g, 5, 5) == doctest::Approx(3));
}

TEST_CASE("held corner points stay: land refused, and the edge of a cliff")
{
    save::TerrainGrid g = flat(10);
    g.at(5, 5).corners = { 4, 4, 4, 4 }; // A raised tile, with cliffs all round
    const CornerHeights before = corner_heights(g, [](int col, int) { return col < 3 || col > 4; });
    shape_ground(g, before, GroundShape::Canyon, { 5, 5 }, 4, { 1, 0 }, 3);
    CHECK(g.at(5, 5).corners[0] == doctest::Approx(4)); // The cliff's top stays
    CHECK(point(g, 4, 5) == doctest::Approx(2));        // Touches a refused tile
    CHECK(point(g, 7, 7) < 2);                          // Free, inside the reach
}
