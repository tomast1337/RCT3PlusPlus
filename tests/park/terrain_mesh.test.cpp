// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/terrain_mesh.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid flat(int width, int height, float level = 0)
    {
        save::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.origin_x = -8;
        g.origin_z = 4;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        save::TerrainCell cell;
        cell.corners = { level, level, level, level };
        g.cells.assign(static_cast<std::size_t>(width * height), cell);
        return g;
    }

    std::uint32_t count(const TerrainMesh& m, GroundPaint paint, std::uint8_t index)
    {
        for (const GroundBatch& b : m.batches)
        {
            if (b.paint == paint && b.index == index)
                return b.count;
        }
        return 0;
    }
} // namespace

TEST_CASE("each tile is two triangles, batched by its surface")
{
    save::TerrainGrid g = flat(3, 2);
    g.cells[4].surface = 7;
    const TerrainMesh m = terrain_mesh(g, { });
    CHECK(count(m, GroundPaint::Surface, 0) == 5 * 6);
    CHECK(count(m, GroundPaint::Surface, 7) == 6);
    CHECK(m.indices.size() == 6 * 6);
    CHECK(m.low.x == doctest::Approx(-8));
    CHECK(m.high.x == doctest::Approx(4));
    CHECK(m.high.z == doctest::Approx(12));
    // Flat ground faces the sun's height alone; every vertex the same shade.
    CHECK(m.vertices[0].colour.r == doctest::Approx(m.vertices[5].colour.r));
}

TEST_CASE("a tile raised above its neighbours gets walls painted with its cliff face")
{
    save::TerrainGrid g = flat(3, 3);
    save::TerrainCell& middle = g.cells[4];
    middle.corners = { 2, 2, 2, 2 };
    middle.cliff = 3;
    const TerrainMesh m = terrain_mesh(g, { });
    // Four sides, two triangles each.
    CHECK(count(m, GroundPaint::Cliff, 3) == 4 * 6);
    CHECK(m.high.y == doctest::Approx(2));
}

TEST_CASE("water covers the triangles of its bodies at their level")
{
    const save::TerrainGrid g = flat(2, 2, -1);
    save::WaterLayer water;
    water.bodies.push_back({ 0.5f, { { 1, 0, 0, 7 }, { 1, 0, 1, 7 }, { 9, 9, 0, 7 } } });
    const TerrainMesh m = terrain_mesh(g, water);
    REQUIRE(m.water.size() == 6); // The tile off the map is left out
    for (const WaterVertex& v : m.water)
        CHECK(v.position.y == doctest::Approx(0.5f));
    CHECK(m.water[0].position.x == doctest::Approx(-4));
}

TEST_CASE("a grid whose cells do not match its size makes nothing")
{
    save::TerrainGrid g = flat(2, 2);
    g.cells.pop_back();
    CHECK(terrain_mesh(g, { }).vertices.empty());
}
