// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path_tunnels.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid flat(int width, int height)
    {
        save::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(width * height));
        return g;
    }

    void raise(save::TerrainGrid& g, int col, int row, float height, std::uint8_t cliff = 2)
    {
        save::TerrainCell& c = g.cells[static_cast<std::size_t>(row * g.width + col)];
        c.corners = { height, height, height, height };
        c.cliff = cliff;
    }

    save::PathTile tunnel(int col, int row, std::int32_t height)
    {
        save::PathTile p;
        p.kind = save::PathKind::Flying;
        p.col = static_cast<std::uint8_t>(col);
        p.row = static_cast<std::uint8_t>(row);
        p.elevation = save::PathElevation { };
        p.elevation->quantised_height = height;
        p.elevation->underground = true;
        return p;
    }

    std::vector<glm::vec3> cliff_points(const TerrainMesh& m)
    {
        std::vector<glm::vec3> out;
        for (const GroundBatch& b : m.batches)
            if (b.paint == GroundPaint::Cliff)
                for (std::uint32_t i = b.first; i < b.first + b.count; ++i)
                    out.push_back(m.vertices[m.indices[i]].position);
        return out;
    }
} // namespace

TEST_CASE("a tunnel piece hollows its tile, unless the ground over it was lowered")
{
    save::TerrainGrid g = flat(3, 3);
    raise(g, 1, 1, 6);
    const std::vector<save::PathTile> paths { tunnel(1, 1, 0), tunnel(0, 0, 0) };
    const auto hollows = tunnel_hollows(paths, g);
    REQUIRE(hollows.size() == 1); // (0, 0) has no ground over it
    CHECK(hollows[0].col == 1);
    CHECK(hollows[0].low == doctest::Approx(-kTunnelFloor));
    CHECK(hollows[0].high == doctest::Approx(kTunnelHeight));
}

TEST_CASE("the walls round a hollowed tile leave the hollow open")
{
    save::TerrainGrid g = flat(3, 3);
    raise(g, 1, 1, 6);
    const auto whole = cliff_points(terrain_mesh(g, { }));
    CHECK(std::ranges::any_of(whole, [](glm::vec3 p) { return p.y < 1; }));
    const std::vector<GroundOpening> hollow { { 1, 1, -kTunnelFloor, kTunnelHeight } };
    const auto open = cliff_points(terrain_mesh(g, { }, hollow));
    CHECK(open.size() == whole.size()); // Each wall keeps its part above the hollow
    CHECK(std::ranges::all_of(open, [](glm::vec3 p) { return p.y >= kTunnelHeight - 0.001f; }));
}

TEST_CASE("a tunnel is lined with a ceiling, and walls where earth lies beyond")
{
    // Two tunnel tiles in a row under a plateau, the row's ends opening onto low ground.
    save::TerrainGrid g = flat(4, 3);
    for (int col = 1; col <= 2; ++col)
        for (int row = 0; row < 3; ++row)
            raise(g, col, row, 6);
    const std::vector<GroundOpening> hollows { { 1, 1, 0, kTunnelHeight }, { 2, 1, 0, kTunnelHeight } };
    const TerrainMesh lining = tunnel_lining(hollows, g);
    // Two ceilings and the two long sides of each tile; the ends are the mouths, the middle is shared.
    CHECK(lining.indices.size() == 6 * (2 + 4));
    REQUIRE(lining.batches.size() == 2);
    CHECK(lining.batches[0].paint == GroundPaint::TunnelWall);
    CHECK(lining.batches[1].paint == GroundPaint::TunnelCeiling);
    CHECK(lining.batches[0].index == 2);
    const auto ceiling = std::ranges::count_if(lining.vertices, [](const GroundVertex& v) { return v.normal.y < 0; });
    CHECK(ceiling == 12);
    // Side walls face into the hollow.
    for (const GroundVertex& v : lining.vertices)
        if (v.normal.y == 0)
            CHECK((v.position.z - 6) * v.normal.z < 0);
}
