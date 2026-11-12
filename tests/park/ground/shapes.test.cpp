// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground/shapes.hpp"

#include <cmath>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;
using namespace rct3::park::ground;

namespace
{
    /// Flat at height 2, `size` tiles each way.
    save::model::TerrainGrid flat(int size)
    {
        save::model::TerrainGrid g;
        g.width = size;
        g.height = size;
        g.cells.resize(static_cast<std::size_t>(size * size));
        for (save::model::TerrainCell& c : g.cells)
            c.corners = { 2, 2, 2, 2 };
        return g;
    }

    float point(const save::model::TerrainGrid& g, int c, int r)
    {
        return g.at(c, r).corners[0]; // The tile's own first corner is the point (c, r)
    }

    const auto anywhere = [](save::model::Tile) { return true; };

    bool near(float value, float wanted, float slack)
    {
        return std::abs(value - wanted) <= slack + 1e-4f;
    }
} // namespace

TEST_CASE("a dragged brush waits out the dead zone, then moves evenly with the pointer, either way")
{
    CHECK(drag_amount(0) == 0);
    CHECK(drag_amount(kDragDeadZone) == 0);
    CHECK(drag_amount(-kDragDeadZone) == 0);
    CHECK(drag_amount(kDragDeadZone + 10) == doctest::Approx(10 * kDragRate));
    CHECK(drag_amount(kDragDeadZone + 10.5f) > drag_amount(kDragDeadZone + 10)); // No steps
    CHECK(drag_amount(-kDragDeadZone - 40) == doctest::Approx(-40 * kDragRate));
}

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
    CHECK(shape_share(GroundShape::Canyon, 0.25f) == doctest::Approx(-1));                  // Flat floor
    CHECK(shape_share(GroundShape::Canyon, 0, 0.8f) < 0);                                   // Long along its line
    CHECK(shape_share(GroundShape::Canyon, 0.5f, 0) == doctest::Approx(0));                 // Narrow across
    CHECK(shape_share(GroundShape::Trough, 0) == doctest::Approx(-1));
    CHECK(shape_share(GroundShape::Crater, 0) == doctest::Approx(-1));
    CHECK(shape_share(GroundShape::Crater, 0.8f) > 0); // The rim
    CHECK(shape_share(GroundShape::Crater, 1.2f) == 0);
}

TEST_CASE("a hill raises the corner points round its middle, shared by the tiles, from the stroke's start")
{
    save::model::TerrainGrid g = flat(10);
    const GroundBefore before = ground_before(g, anywhere);
    const float moved = shape_ground(g, before, GroundShape::Hill, { 5, 5 }, 3, { 1, 0 }, 4, 1);
    CHECK(moved > 0);
    CHECK(near(point(g, 5, 5), 6, 0.5f * shape_roughness(GroundShape::Hill) * 4));
    CHECK(g.at(4, 4).corners[3] == point(g, 5, 5)); // The same point, as the tile up and left holds it
    CHECK(point(g, 6, 5) > 2);
    CHECK(point(g, 6, 5) < 6);
    CHECK(point(g, 9, 5) == doctest::Approx(2)); // Past the reach

    // A smaller stroke from the same start replaces the bigger one, as the pointer moving back does.
    shape_ground(g, before, GroundShape::Hill, { 5, 5 }, 3, { 1, 0 }, 1, 1);
    CHECK(near(point(g, 5, 5), 3, 0.5f * shape_roughness(GroundShape::Hill)));
}

TEST_CASE("a negative amount turns the shape over: a mountain digs a pit, a crater builds a mound")
{
    save::model::TerrainGrid g = flat(10);
    const GroundBefore before = ground_before(g, anywhere);
    const float dug = shape_ground(g, before, GroundShape::Mountain, { 5, 5 }, 3, { 1, 0 }, -2, 1);
    CHECK(near(point(g, 5, 5), 0, 0.5f * shape_roughness(GroundShape::Mountain) * 2));
    CHECK(dug == doctest::Approx(shape_ground(g, before, GroundShape::Mountain, { 5, 5 }, 3, { 1, 0 }, 2, 1))); // Paid alike

    shape_ground(g, before, GroundShape::Crater, { 5, 5 }, 3, { 1, 0 }, -2, 1);
    CHECK(near(point(g, 5, 5), 4, 0.5f * shape_roughness(GroundShape::Crater) * 2));
}

TEST_CASE("corner points touching refused land stay")
{
    save::model::TerrainGrid g = flat(10);
    const GroundBefore before = ground_before(g, [](save::model::Tile t) { return t.col < 3 || t.col > 4; });
    shape_ground(g, before, GroundShape::Canyon, { 5, 5 }, 4, { 1, 0 }, 3, 1);
    CHECK(point(g, 4, 5) == doctest::Approx(2)); // Touches a refused tile
    CHECK(point(g, 5, 5) == doctest::Approx(2)); // Its other side too, so no cliff opens along it
    CHECK(point(g, 7, 5) < 2);                   // Free, along the canyon inside the reach
}

TEST_CASE("a cliff under a brush rides with the ground and keeps its height")
{
    save::model::TerrainGrid g = flat(10);
    g.at(5, 5).corners = { 4, 4, 4, 4 }; // A raised tile, with cliffs two units high all round
    const GroundBefore before = ground_before(g, anywhere);
    const float moved = shape_ground(g, before, GroundShape::Mesa, { 5.5f, 5.5f }, 4, { 1, 0 }, 3, 1);
    const float slack = 0.5f * shape_roughness(GroundShape::Mesa) * 3;
    for (const float h : g.at(5, 5).corners)
        CHECK(near(h, 7, slack));                                               // The top went up with the mesa
    CHECK(near(g.at(6, 5).corners[0], 5, slack));                               // And so did the foot beside it
    CHECK(g.at(5, 5).corners[1] - g.at(6, 5).corners[0] == doctest::Approx(2)); // The cliff as high as it was
    CHECK(near(g.at(4, 4).corners[3], 5, slack));
    // Every tile's own corner is counted: the four points of the raised tile are each held by four tiles.
    CHECK(moved >= 16 * (3 - slack));
}

TEST_CASE("no shape is quite smooth: each corner point is a little off it, the same for the whole stroke")
{
    save::model::TerrainGrid g = flat(12);
    const GroundBefore before = ground_before(g, anywhere);
    shape_ground(g, before, GroundShape::Mountain, { 6, 6 }, 4, { 1, 0 }, 10, 1);
    // Two points the same distance out differ, within the shape's roughness.
    const float east = point(g, 7, 6);
    const float west = point(g, 5, 6);
    CHECK(east != west);
    CHECK(std::abs(east - west) <= shape_roughness(GroundShape::Mountain) * 10);
    // The tiles round a point still agree on it.
    CHECK(g.at(6, 6).corners[1] == g.at(7, 6).corners[0]);
    CHECK(g.at(6, 5).corners[3] == g.at(7, 6).corners[0]);
    // The same stroke again, further: every point keeps its draw, so the ground grows without flickering.
    save::model::TerrainGrid twice = flat(12);
    shape_ground(twice, before, GroundShape::Mountain, { 6, 6 }, 4, { 1, 0 }, 20, 1);
    CHECK(point(twice, 7, 6) - 2 == doctest::Approx(2 * (east - 2)));
    // Another stroke draws afresh.
    save::model::TerrainGrid other = flat(12);
    shape_ground(other, before, GroundShape::Mountain, { 6, 6 }, 4, { 1, 0 }, 10, 2);
    CHECK(point(other, 7, 6) != east);
    // A hill is all but smooth.
    CHECK(shape_roughness(GroundShape::Hill) < shape_roughness(GroundShape::Mountain));
}
