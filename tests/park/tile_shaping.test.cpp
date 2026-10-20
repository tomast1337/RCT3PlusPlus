// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/tile_shaping.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid flat_grid(int width, int height, float at)
    {
        save::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(width * height));
        for (save::TerrainCell& c : g.cells)
            c.corners = { at, at, at, at };
        return g;
    }

    bool anywhere(save::Tile)
    {
        return true;
    }

    constexpr std::array<float, 4> kLevelTwo { 2, 2, 2, 2 };
    constexpr std::size_t kStripVertices = 4;
} // namespace

TEST_CASE("the pointer's place in a tile picks the whole tile, an edge or a corner")
{
    CHECK(part_at({ 0.5f, 0.5f }) == TilePart::Whole);
    CHECK(part_at({ 0.1f, 0.5f }) == TilePart::Left);
    CHECK(part_at({ 0.9f, 0.5f }) == TilePart::Right);
    CHECK(part_at({ 0.5f, 0.1f }) == TilePart::Near);
    CHECK(part_at({ 0.5f, 0.9f }) == TilePart::Far);
    CHECK(part_at({ 0.1f, 0.1f }) == TilePart::NearLeft);
    CHECK(part_at({ 0.9f, 0.9f }) == TilePart::FarRight);
}

TEST_CASE("a tile moves alone, its neighbours keep their corners")
{
    save::TerrainGrid g = flat_grid(3, 3, 2);
    const TileStroke s = begin_tile_stroke(g, 1, 1, 1, 1, TilePart::Whole);
    const float moved = shape_tiles(g, s, TileShaping::Free, 1.5f, anywhere);
    CHECK(g.at(1, 1).corners == std::array<float, 4> { 3.5f, 3.5f, 3.5f, 3.5f });
    CHECK(g.at(0, 1).corners == kLevelTwo);
    CHECK(g.at(2, 2).corners == kLevelTwo);
    CHECK(moved == doctest::Approx(6));
    // Back to where it began, whatever the stroke passed through.
    shape_tiles(g, s, TileShaping::Free, 0, anywhere);
    CHECK(g.at(1, 1).corners == kLevelTwo);
}

TEST_CASE("an edge or a corner moves only its own corners, and a slope lifts from below")
{
    save::TerrainGrid g = flat_grid(2, 1, 2);
    shape_tiles(g, begin_tile_stroke(g, 0, 0, 0, 0, TilePart::Right), TileShaping::Free, 1, anywhere);
    CHECK(g.at(0, 0).corners == std::array<float, 4> { 2, 3, 2, 3 });
    shape_tiles(g, begin_tile_stroke(g, 1, 0, 1, 0, TilePart::FarLeft), TileShaping::Free, -1, anywhere);
    CHECK(g.at(1, 0).corners == std::array<float, 4> { 2, 2, 1, 2 });
    // The sloped tile, lifted by half: its low corners rise to 2.5, its high ones stay.
    shape_tiles(g, begin_tile_stroke(g, 0, 0, 0, 0, TilePart::Whole), TileShaping::Free, 0.5f, anywhere);
    CHECK(g.at(0, 0).corners == std::array<float, 4> { 2.5f, 3, 2.5f, 3 });
}

TEST_CASE("whole units round, and snapping catches on the heights round the corner")
{
    save::TerrainGrid g = flat_grid(2, 1, 2);
    shape_tiles(g, begin_tile_stroke(g, 0, 0, 0, 0, TilePart::Whole), TileShaping::Whole, 1.25f, anywhere);
    CHECK(g.at(0, 0).corners == std::array<float, 4> { 3, 3, 3, 3 });

    g = flat_grid(2, 1, 2);
    g.at(1, 0).corners = { 5, 5, 5, 5 };
    const TileStroke s = begin_tile_stroke(g, 0, 0, 0, 0, TilePart::Whole);
    shape_tiles(g, s, TileShaping::Snap, 2.75f, anywhere);
    // The right edge meets the neighbour's left edge; the left edge has nothing near and moves freely.
    CHECK(g.at(0, 0).corners == std::array<float, 4> { 4.75f, 5, 4.75f, 5 });
}

TEST_CASE("refused tiles keep their corners")
{
    save::TerrainGrid g = flat_grid(3, 1, 2);
    const TileStroke s = begin_tile_stroke(g, 0, 0, 2, 0, TilePart::Whole);
    shape_tiles(g, s, TileShaping::Free, 1, [](save::Tile t) { return t.col != 1; });
    CHECK(g.at(0, 0).corners[0] == 3);
    CHECK(g.at(1, 0).corners == kLevelTwo);
    CHECK(g.at(2, 0).corners[0] == 3);
}

TEST_CASE("marks outline whole tiles, one edge, or cross one corner")
{
    const save::TerrainGrid g = flat_grid(3, 3, 2);
    TerrainMesh mesh;
    tile_marks(mesh, g, 0, 0, 1, 1, TilePart::Whole, glm::vec4(1));
    CHECK(mesh.vertices.size() == 4 * 4 * kStripVertices);
    mesh = { };
    tile_marks(mesh, g, 1, 1, 1, 1, TilePart::Near, glm::vec4(1));
    CHECK(mesh.vertices.size() == kStripVertices);
    mesh = { };
    tile_marks(mesh, g, 1, 1, 1, 1, TilePart::FarRight, glm::vec4(1));
    CHECK(mesh.vertices.size() == 2 * kStripVertices);
}
