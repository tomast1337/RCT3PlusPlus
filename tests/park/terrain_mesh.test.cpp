// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/terrain_mesh.hpp"

#include <doctest/doctest.h>
#include <vector>

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
    const save::TerrainGrid g = flat(3, 2);
    const TerrainMesh m = terrain_mesh(g, { });
    CHECK(count(m, GroundPaint::Surface, 0) == 6 * 6);
    CHECK(m.indices.size() == 6 * 6);
    for (const GroundVertex& v : m.vertices)
        CHECK(v.colour.a == doctest::Approx(1));
    CHECK(m.low.x == doctest::Approx(-8));
    CHECK(m.high.x == doctest::Approx(4));
    CHECK(m.high.z == doctest::Approx(12));
    // Flat ground faces the sun's height alone; every vertex the same shade.
    CHECK(m.vertices[0].colour.r == doctest::Approx(m.vertices[5].colour.r));
}

TEST_CASE("surfaces fade into each other across the tiles next to them")
{
    // Two tiles side by side: surface 0, then surface 7.
    save::TerrainGrid g = flat(2, 1);
    g.cells[1].surface = 7;
    const TerrainMesh m = terrain_mesh(g, { });
    // Both tiles touch both surfaces, so each is drawn twice.
    CHECK(count(m, GroundPaint::Surface, 0) == 2 * 6);
    CHECK(count(m, GroundPaint::Surface, 7) == 2 * 6);
    REQUIRE(m.batches.at(0).index == 0); // Lowest first

    // The second layer covers half of the first on the shared edge and nothing on the far edges.
    const GroundBatch& top = m.batches.at(1);
    for (std::uint32_t i = top.first; i < top.first + top.count; ++i)
    {
        const GroundVertex& v = m.vertices[m.indices[i]];
        if (v.position.x == doctest::Approx(-8))
            CHECK(v.colour.a == doctest::Approx(0));
        else if (v.position.x == doctest::Approx(-4))
            CHECK(v.colour.a == doctest::Approx(0.5f));
        else
            CHECK(v.colour.a == doctest::Approx(1));
    }
    // The first layer is opaque everywhere.
    const GroundBatch& bottom = m.batches.at(0);
    for (std::uint32_t i = bottom.first; i < bottom.first + bottom.count; ++i)
        CHECK(m.vertices[m.indices[i]].colour.a == doctest::Approx(1));
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

TEST_CASE("ground shades smoothly where tiles meet, and sharply at cliffs")
{
    // Flat at 0, a slope up to 4, then flat at `top`.
    // Shades of the ground vertices at grid point x = 0 (where the slope meets the top), z = 4, height y.
    const auto shades_at = [](float top, float x, float y) {
        save::TerrainGrid g = flat(3, 1);
        g.cells[1].corners = { 0, 4, 0, 4 };
        g.cells[2].corners = { top, top, top, top };
        const TerrainMesh m = terrain_mesh(g, { });
        std::vector<float> shades;
        for (const GroundBatch& b : m.batches)
        {
            if (b.paint != GroundPaint::Surface)
                continue;
            for (std::uint32_t i = b.first; i < b.first + b.count; ++i)
            {
                const GroundVertex& v = m.vertices[m.indices[i]];
                if (v.position.x == doctest::Approx(x) && v.position.y == doctest::Approx(y) && v.position.z == doctest::Approx(4))
                    shades.push_back(v.colour.r);
            }
        }
        return shades;
    };
    const float flat_shade = shades_at(4, -8, 0).front(); // Far corner of the first tile, flat all round

    // Where the slope meets the top, both tiles agree on one shade, a mix of flat and sloped.
    const std::vector<float> joined = shades_at(4, 0, 4);
    REQUIRE(joined.size() >= 2);
    for (const float s : joined)
        CHECK(s == doctest::Approx(joined.front()));
    CHECK(joined.front() != doctest::Approx(flat_shade));

    // With a cliff up to 8, the top keeps its flat shade and the slope does not borrow it.
    const std::vector<float> top = shades_at(8, 0, 8);
    REQUIRE_FALSE(top.empty());
    CHECK(top.front() == doctest::Approx(flat_shade));
    const std::vector<float> below = shades_at(8, 0, 4);
    REQUIRE_FALSE(below.empty());
    CHECK(below.front() != doctest::Approx(joined.front()));
}
