// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/enclosures.hpp"

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

} // namespace

TEST_CASE("enclosures: a fence panel on every edge of an enclosure, inside it")
{
    const save::TerrainGrid g = flat(5, 5);
    save::EnclosureMap map;
    CHECK(enclose(map, g, { 1, 1 }, { 2, 3 }, "Wood") == 0);
    CHECK(map.cols == 5);
    CHECK(enclosure_size(map, 0) == 6);
    const BoundaryFence fence = enclosure_fences(map, g, 100);
    // Two by three: 2 + 2 + 3 + 3 edges.
    REQUIRE(fence.pieces.size() == 10);
    CHECK(fence.pieces.front().id == 101);
    CHECK(fence.pieces.front().symbol == "WoodEdge");
    CHECK(fence.pieces.front().overlay == "Enclosures/Fences/Wood/Wood_Stub");
    CHECK(std::ranges::all_of(fence.pieces, [](const save::PlacedObject& o) { return o.fixture && o.col >= 1 && o.col <= 2 && o.row >= 1 && o.row <= 3; }));
}

TEST_CASE("enclosures: an area touching an enclosure joins it and keeps its fence")
{
    const save::TerrainGrid g = flat(6, 6);
    save::EnclosureMap map;
    enclose(map, g, { 0, 0 }, { 1, 1 }, "Wood");
    CHECK(enclose(map, g, { 2, 0 }, { 3, 1 }, "Electric") == 0);
    CHECK(enclosure_size(map, 0) == 8);
    CHECK(map.fences == std::vector<std::string> { "Wood" });
    CHECK(enclosure_fences(map, g, 0).pieces.size() == 12);
    // Only corner to corner is apart.
    CHECK(enclose(map, g, { 4, 2 }, { 4, 2 }, "Electric") == 1);
}

TEST_CASE("enclosures: two that touch share one fence; an area across both makes one")
{
    const save::TerrainGrid g = flat(6, 6);
    save::EnclosureMap map;
    map = { .cols = 3, .rows = 1, .tiles = { 0, 1, 1 }, .fences = { "Wood", "Chain" } };
    // 4 round the first, 6 round the second, less the shared one.
    CHECK(enclosure_fences(map, g, 0).pieces.size() == 9);
    enclose(map, g, { 0, 0 }, { 2, 0 }, "Chain");
    CHECK(map.tiles == std::vector<std::int32_t> { 0, 0, 0 });
    CHECK(map.fences == std::vector<std::string> { "Wood", "" });
    CHECK(enclose(map, g, { 0, 0 }, { 0, 0 }, "Chain") == 0);
}

TEST_CASE("enclosures: taking out every tile ends the enclosure, its place used again")
{
    const save::TerrainGrid g = flat(5, 5);
    save::EnclosureMap map;
    enclose(map, g, { 0, 0 }, { 1, 0 }, "Wood");
    enclose(map, g, { 3, 3 }, { 4, 4 }, "Chain");
    unenclose(map, { 1, 0 }, { 1, 0 });
    CHECK(enclosure_size(map, 0) == 1);
    unenclose(map, { -2, -2 }, { 0, 0 });
    CHECK(map.fences == std::vector<std::string> { "", "Chain" });
    CHECK(enclose(map, g, { 0, 4 }, { 0, 4 }, "Electric") == 0);
    CHECK(enclosure_fences(map, g, 0).pieces.size() == 12);
}
