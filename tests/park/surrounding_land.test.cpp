// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/surrounding_land.hpp"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    /// A `w` by `h` map of flat tiles at height `y`, painted with surface 1.
    save::TerrainGrid flat(int w, int h, float y)
    {
        save::TerrainGrid g;
        g.width = w;
        g.height = h;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(w * h));
        for (auto& c : g.cells)
        {
            c.corners = { y, y, y, y };
            c.surface = 1;
        }
        return g;
    }

    save::TerrainCell& cell(save::TerrainGrid& g, int col, int row)
    {
        return g.cells[static_cast<std::size_t>(row * g.width + col)];
    }
} // namespace

TEST_CASE("the land round a flat map stays at its height and fades out")
{
    const save::TerrainGrid g = flat(3, 2, 5);
    const TerrainMesh m = surrounding_land(g);
    REQUIRE_FALSE(m.vertices.empty());
    REQUIRE(m.batches.size() == 1);
    CHECK(m.batches[0].paint == GroundPaint::Surface);
    CHECK(m.batches[0].index == 1);
    CHECK(m.batches[0].count == m.indices.size());
    for (const GroundVertex& v : m.vertices)
        CHECK(v.position.y == doctest::Approx(5));
    // The first ring is the map's edge, opaque; the farthest is clear, kSurroundReach out.
    CHECK(m.vertices.front().position == glm::vec3(0, 5, 0));
    CHECK(m.vertices.front().colour.a == 1);
    CHECK(m.vertices.back().colour.a == doctest::Approx(0));
    CHECK(m.low.x == doctest::Approx(-kSurroundReach));
    CHECK(m.high.z == doctest::Approx(8 + kSurroundReach));
    CHECK(edge_level(g) == doctest::Approx(5));
    // Every index is in range, in whole triangles.
    CHECK(m.indices.size() % 3 == 0);
    CHECK(std::ranges::all_of(m.indices, [&](std::uint32_t i) { return i < m.vertices.size(); }));
}

TEST_CASE("a hill at the edge rolls on and settles at the edge's mean height")
{
    save::TerrainGrid g = flat(4, 4, 0);
    // Raise the corner at the middle of the first row's edge.
    cell(g, 1, 0).corners[1] = 40;
    cell(g, 2, 0).corners[0] = 40;
    const float level = edge_level(g);
    CHECK(level == doctest::Approx(40.0f / 16));
    const TerrainMesh m = surrounding_land(g);
    REQUIRE_FALSE(m.vertices.empty());
    // The edge point itself is the hill's top; the last ring is near the mean.
    const float top = std::ranges::max(m.vertices, { }, [](const GroundVertex& v) { return v.position.y; }).position.y;
    CHECK(top == doctest::Approx(40));
    const std::size_t ring = 16; // Points round a 4 by 4 map's edge
    for (std::size_t i = m.vertices.size() - ring; i < m.vertices.size(); ++i)
        CHECK(m.vertices[i].position.y == doctest::Approx(level).epsilon(0.05));
}

TEST_CASE("the edge shows the park's mix of surfaces, which gives way to the commonest going out")
{
    save::TerrainGrid g = flat(4, 4, 0);
    cell(g, 1, 0).surface = 7;
    const TerrainMesh m = surrounding_land(g);
    REQUIRE(m.batches.size() == 2);
    CHECK(m.batches[0].index == 1);
    CHECK(m.batches[1].index == 7);
    CHECK(m.batches[1].count < m.batches[0].count);
    const std::size_t points = m.vertices.size() / 2; // Each layer has every point
    // Edge point 1 touches tiles 0 and 1 of the first row: half of each. The upper layer covers half of
    // the lower, which is opaque.
    CHECK(m.vertices[1].colour.a == doctest::Approx(1));
    CHECK(m.vertices[points + 1].colour.a == doctest::Approx(0.5f));
    // Far out, but before the fade, there is next to none of surface 7.
    const std::size_t ring = 16;
    std::size_t k = 1;
    while (m.vertices[(k + 1) * ring].position.x > -kSurroundFadeStart)
        ++k;
    CHECK(m.vertices[points + k * ring + 1].colour.a < 0.01f);
    CHECK(m.vertices[k * ring + 1].colour.a == doctest::Approx(1));
}

TEST_CASE("the land at the edge is lit as the park's ground there")
{
    save::TerrainGrid g = flat(3, 3, 0);
    cell(g, 0, 0).corners = { 0, 4, 0, 4 }; // A slope rising along X
    const TerrainMesh m = surrounding_land(g);
    REQUIRE_FALSE(m.vertices.empty());
    const glm::vec3 lit = glm::normalize(m.vertices[0].normal);
    const glm::vec3 there = glm::normalize(ground_normal(g, { 0, 0 }, 0));
    CHECK(lit.x == doctest::Approx(there.x));
    CHECK(lit.y == doctest::Approx(there.y));
    CHECK(lit.z == doctest::Approx(there.z));
    CHECK(lit.y < 0.99f);
    for (const GroundVertex& v : m.vertices)
        CHECK(v.normal.y > 0);
}

TEST_CASE("an empty map has no land round it")
{
    CHECK(surrounding_land({ }).vertices.empty());
    CHECK(edge_level({ }) == 0);
}

TEST_CASE("land added to the ground follows it")
{
    TerrainMesh ground;
    ground.vertices.resize(3);
    ground.indices = { 0, 1, 2 };
    ground.batches = { { GroundPaint::Surface, 0, 0, 3 } };
    ground.low = glm::vec3(0.0f);
    ground.high = glm::vec3(1.0f);
    const TerrainMesh land = surrounding_land(flat(2, 2, 0));
    add_ground(ground, land);
    REQUIRE(ground.batches.size() == 2);
    CHECK(ground.batches[1].first == 3);
    CHECK(ground.indices[3] == land.indices[0] + 3);
    CHECK(ground.vertices.size() == 3 + land.vertices.size());
    CHECK(ground.low.x == doctest::Approx(-kSurroundReach));
    CHECK(ground.high.y == doctest::Approx(1));
}

TEST_CASE("the land's height is found between its points")
{
    save::TerrainGrid g = flat(4, 4, 2);
    cell(g, 0, 0).corners = { 6, 6, 6, 6 }; // The corner tile stands higher
    const LandRings rings = land_rings(g);
    REQUIRE(rings.points == 16);
    CHECK(rings.heights.size() == rings.points * rings.distances.size());
    // On the map, or past the land: nothing.
    CHECK_FALSE(land_height(rings, g, 8, 8));
    CHECK_FALSE(land_height(rings, g, -kSurroundReach - 1, 8));
    // Right by the edge: at the high tile's corner, an eighth of the way on to the next corner, and away.
    CHECK(*land_height(rings, g, 0, -0.001f) == doctest::Approx(6).epsilon(0.01));
    CHECK(*land_height(rings, g, 0.5f, -0.001f) == doctest::Approx(5.5f).epsilon(0.01));
    CHECK(*land_height(rings, g, 12, -0.001f) == doctest::Approx(2).epsilon(0.01));
    // Far out, the mean of the edge.
    CHECK(*land_height(rings, g, 8, -kSurroundReach + 1) == doctest::Approx(edge_level(g)).epsilon(0.02));
    // The mesh's points agree with the rings.
    const TerrainMesh m = surrounding_land(g);
    CHECK(m.vertices[rings.points + 3].position.y == doctest::Approx(rings.heights[rings.points + 3]));
}

TEST_CASE("water over the ground at the edge goes on out as a sea, with the land kept under it")
{
    // A 16 by 16 map at height 2 with a lake at 3 over its first row: the -z side.
    const save::TerrainGrid g = flat(16, 16, 2);
    save::WaterLayer water;
    save::WaterBody& lake = water.bodies.emplace_back();
    lake.level = 3;
    for (std::uint8_t c = 0; c < 16; ++c)
        for (std::uint8_t t = 0; t < 2; ++t)
            lake.tris.push_back({ .col = c, .row = 0, .triangle = t });
    const LandRings rings = land_rings(g, water);
    CHECK(rings.sea[1] == 3);
    CHECK(std::isnan(rings.sea[20]));
    // The land beside it comes down to it over the next few points round the corner, under its water.
    CHECK(rings.shore[20] == 3);
    CHECK(std::isnan(rings.shore[16 + kShorePoints]));
    CHECK(std::isnan(rings.shore[40])); // The far side, dry
    CHECK(rings.heights[rings.points * 2 + 1] <= 3 - kSeaDepth);
    CHECK(rings.heights[rings.points * 2 + 18] < 3);
    CHECK_FALSE(land_height(rings, g, 30, -50)); // Out at sea: no land for trees
    CHECK(land_height(rings, g, 30, 64 + 50));

    const TerrainMesh m = surrounding_land(g, water);
    REQUIRE_FALSE(m.water.empty());
    CHECK(m.water.size() % 3 == 0);
    CHECK(std::ranges::all_of(m.water, [](const WaterVertex& v) { return v.position.y == 3 && v.position.z < 64; }));
    CHECK(std::ranges::min(m.water, { }, [](const WaterVertex& v) { return v.position.z; }).position.z == doctest::Approx(-kSeaReach));
    CHECK(m.low.z == doctest::Approx(-kSeaReach));
    // Dry edges make no sea.
    CHECK(surrounding_land(g).water.empty());
}
