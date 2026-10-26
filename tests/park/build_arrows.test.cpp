// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/build_arrows.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <ranges>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid grid_of_fours()
    {
        save::TerrainGrid g;
        g.width = 8;
        g.height = 8;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        return g;
    }
} // namespace

TEST_CASE("a stall's arrow lies across the edge it faces, its head on the outer half")
{
    const save::TerrainGrid g = grid_of_fours();
    // Facing higher columns from tile (2, 3): the quad spans x 10..14, over the edge at x 12.
    const TerrainMesh m = facing_arrow(2, 3, 5.0f, 3, g);
    REQUIRE(m.vertices.size() == 4);
    for (const GroundVertex& v : m.vertices)
    {
        CHECK(v.position.y == doctest::Approx(5.0f));
        CHECK(v.position.z >= 12.0f);
        CHECK(v.position.z <= 16.0f);
        // The picture's bottom edge (v = 1), where the head points, is the outer edge.
        CHECK(v.uv.y == doctest::Approx(v.position.x > 12.0f ? 1.0f : 0.0f));
    }
    const auto [low, high] = std::ranges::minmax(m.vertices | std::views::transform([](const GroundVertex& v) { return v.position.x; }));
    CHECK(low == doctest::Approx(10.0f));
    CHECK(high == doctest::Approx(14.0f));

    // Facing lower rows: the outer edge is at the low z.
    for (const GroundVertex& v : facing_arrow(2, 3, 0.0f, 0, g).vertices)
        CHECK(v.uv.y == doctest::Approx(v.position.z < 12.0f ? 1.0f : 0.0f));
}

TEST_CASE("track arrows float over the rails, white at the start, green or red at the end")
{
    const TrackTip start { .position = { 0, 1, 0 }, .end = { .heading = 0 } };
    const TrackTip end { .position = { 4, 1, 0 }, .end = { .heading = 0 } };
    const TerrainMesh m = track_arrows(start, end, true);
    REQUIRE(m.batches.size() == 1);
    CHECK(m.batches[0].count == m.indices.size());
    CHECK(m.vertices.size() == 6 * 4);
    for (const GroundVertex& v : m.vertices)
        CHECK(v.position.y == doctest::Approx(1.2f));
    // The open arrow's head is one unit on along heading 0 (higher x), the triangle's tip on the end itself.
    CHECK(std::ranges::any_of(m.vertices, [](const GroundVertex& v) { return v.colour == glm::vec4(1) && v.position.x > 0.99f; }));
    CHECK(std::ranges::all_of(m.vertices | std::views::drop(12), [](const GroundVertex& v) { return v.colour == glm::vec4(0, 1, 0, 1) && v.position.x <= 4.05f && v.position.x >= 3.25f; }));
    CHECK(track_arrows(start, end, false).vertices.back().colour == glm::vec4(1, 0, 0, 1));
}
