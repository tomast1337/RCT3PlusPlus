// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/enclosures.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <map>

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

    bool any(save::Tile)
    {
        return true;
    }

    /// How many pieces of each kind `fence` has, by the end of their names.
    std::map<std::string, int> kinds(const BoundaryFence& fence, std::string_view kind_of)
    {
        std::map<std::string, int> out;
        for (const save::PlacedObject& o : fence.pieces)
            ++out[o.symbol.substr(kind_of.size())];
        return out;
    }

    FencePrice wood(std::string_view)
    {
        return { .build = 500, .refund = -400 };
    }

} // namespace

TEST_CASE("enclosures: a fenced rectangle takes corner pieces at its corners and panels between")
{
    const save::TerrainGrid g = flat(6, 6);
    save::EnclosureMap map;
    CHECK(enclose(map, g, { 1, 1 }, { 3, 4 }, "Wood", any) == 0);
    CHECK(map.cols == 6);
    CHECK(enclosure_size(map, 0) == 12);
    const BoundaryFence fence = enclosure_fences(map, g, 100);
    CHECK(fence.pieces.front().id == 101);
    CHECK(fence.pieces.front().overlay == "Enclosures/Fences/Wood/Wood_Stub");
    // Three by four: four corners, then 1 + 1 + 2 + 2 panels between.
    CHECK(kinds(fence, "Wood") == std::map<std::string, int> { { "Corner", 4 }, { "Edge", 6 } });
    CHECK(std::ranges::all_of(fence.pieces, [](const save::PlacedObject& o) { return o.fixture && o.col >= 1 && o.col <= 3 && o.row >= 1 && o.row <= 4; }));
    // Each corner piece is turned to a different corner.
    std::vector<std::int32_t> turns;
    for (const save::PlacedObject& o : fence.pieces)
        if (o.symbol == "WoodCorner")
            turns.push_back(o.rotation);
    std::ranges::sort(turns);
    CHECK(turns == std::vector<std::int32_t> { 0, 1, 2, 3 });
}

TEST_CASE("enclosures: one-tile pens, strips and L shapes take full, end, panel pairs and inner posts")
{
    const save::TerrainGrid g = flat(8, 8);
    SUBCASE("one tile")
    {
        save::EnclosureMap map;
        (void)enclose(map, g, { 2, 2 }, { 2, 2 }, "Chain", any);
        CHECK(kinds(enclosure_fences(map, g, 0), "Chain") == std::map<std::string, int> { { "Full", 1 } });
    }
    SUBCASE("a strip one tile wide")
    {
        save::EnclosureMap map;
        (void)enclose(map, g, { 1, 2 }, { 4, 2 }, "Chain", any);
        CHECK(kinds(enclosure_fences(map, g, 0), "Chain") == std::map<std::string, int> { { "End", 2 }, { "Edge", 4 } });
    }
    SUBCASE("an L")
    {
        save::EnclosureMap map;
        (void)enclose(map, g, { 0, 0 }, { 2, 2 }, "Chain", any);
        (void)enclose(map, g, { 3, 0 }, { 4, 0 }, "Chain", any);
        // The inside corner where the arm leaves the square gets a post.
        CHECK(kinds(enclosure_fences(map, g, 0), "Chain").at("CornerI") == 1);
    }
}

TEST_CASE("enclosures: a drag takes only free tiles the test allows")
{
    const save::TerrainGrid g = flat(6, 6);
    save::EnclosureMap map;
    (void)enclose(map, g, { 0, 0 }, { 1, 1 }, "Electric", any);
    const auto no_middle = [](save::Tile t) { return !(t.col == 3 && t.row == 1); };
    CHECK(enclose(map, g, { 1, 0 }, { 4, 2 }, "Wood", no_middle) == 1);
    CHECK(map.at(1, 1) == 0); // Still the electric one's
    CHECK(map.at(3, 1) == -1);
    CHECK(enclosure_size(map, 0) == 4);
    CHECK(enclosure_size(map, 1) == 9);
    CHECK(enclose(map, g, { 5, 5 }, { 5, 5 }, "Wood", [](save::Tile) { return false; }) == -1);
    CHECK(map.fences == std::vector<std::string> { "Electric", "Wood", "" });
}

TEST_CASE("enclosures: touching areas join only when they have the same fence")
{
    const save::TerrainGrid g = flat(6, 6);
    save::EnclosureMap map;
    (void)enclose(map, g, { 0, 0 }, { 1, 1 }, "Wood", any);
    CHECK(enclose(map, g, { 2, 0 }, { 3, 1 }, "Wood", any) == 0);
    CHECK(enclosure_size(map, 0) == 8);
    CHECK(enclose(map, g, { 4, 0 }, { 4, 1 }, "Chain", any) == 1);
    CHECK(map.fences == std::vector<std::string> { "Wood", "Chain" });
    // A fence each along the side they share: 12 lengths round the wood one, 6 round the chain one.
    CHECK(fence_cost({ }, map, [](std::string_view) { return FencePrice { .build = 1 }; }) == 18);
    // Corner to corner is apart.
    CHECK(enclose(map, g, { 5, 2 }, { 5, 2 }, "Chain", any) == 2);
}

TEST_CASE("enclosures: cutting an enclosure in two makes two; the larger part keeps its place")
{
    const save::TerrainGrid g = flat(8, 4);
    save::EnclosureMap map;
    (void)enclose(map, g, { 0, 0 }, { 0, 0 }, "Chain", any);
    (void)enclose(map, g, { 0, 2 }, { 7, 3 }, "Wood", any);
    REQUIRE(map.at(0, 2) == 1);
    unenclose(map, { 2, 0 }, { 2, 3 }, any);
    CHECK(map.at(3, 2) == 1); // Five columns wide
    CHECK(map.at(0, 2) == 2); // Two wide, a new place
    CHECK(map.fences[2] == "Wood");
    unenclose(map, { 0, 0 }, { 0, 0 }, any);
    CHECK(map.fences[0].empty()); // Ended, its place free again
    CHECK(enclose(map, g, { 7, 0 }, { 7, 0 }, "Electric", any) == 0);
}

TEST_CASE("enclosures: tiles the test refuses stay in their enclosure")
{
    const save::TerrainGrid g = flat(5, 5);
    save::EnclosureMap map;
    (void)enclose(map, g, { 0, 0 }, { 2, 2 }, "Wood", any);
    unenclose(map, { 0, 0 }, { 2, 2 }, [](save::Tile t) { return t.col != 1 || t.row != 1; });
    CHECK(map.at(1, 1) == 0);
    CHECK(enclosure_size(map, 0) == 1);
}

TEST_CASE("enclosures: fence put up is paid for, fence taken down paid back")
{
    const save::TerrainGrid g = flat(6, 6);
    save::EnclosureMap none;
    save::EnclosureMap map;
    (void)enclose(map, g, { 0, 0 }, { 1, 1 }, "Wood", any);
    CHECK(fence_cost(none, map, wood) == 8 * 500);
    // Joining: four lengths more, the two lengths between come down.
    save::EnclosureMap bigger = map;
    (void)enclose(bigger, g, { 2, 0 }, { 2, 1 }, "Wood", any);
    CHECK(fence_cost(map, bigger, wood) == 4 * 500 - 2 * 400);
    // Taking out a corner tile: its two lengths come down, two go up round the gap.
    save::EnclosureMap smaller = map;
    unenclose(smaller, { 0, 0 }, { 0, 0 }, any);
    CHECK(fence_cost(map, smaller, wood) == 2 * 500 - 2 * 400);
}

TEST_CASE("enclosures: an animal house fits only wholly inside one enclosure")
{
    const save::TerrainGrid g = flat(8, 8);
    save::EnclosureMap map;
    (void)enclose(map, g, { 0, 0 }, { 3, 3 }, "Wood", any);
    (void)enclose(map, g, { 5, 0 }, { 7, 3 }, "Chain", any);
    CHECK(enclosure_under(map, { 1, 1 }, { 3, 3 }) == 0);
    CHECK(enclosure_under(map, { 2, 2 }, { 3, 3 }) == -1); // Over the edge
    CHECK(enclosure_under(map, { 5, 1 }, { 3, 2 }) == 1);
    CHECK(enclosure_under(map, { 3, 0 }, { 3, 1 }) == -1); // Across the gap into the other
    CHECK(enclosure_under(map, { 0, 5 }, { 1, 1 }) == -1);
}

TEST_CASE("enclosures: pieces round more than one side lean with both sides of their corner")
{
    save::TerrainGrid g = flat(3, 3);
    g.at(1, 1).corners = { 0, 2, 0, 2 }; // Rising along x
    save::PlacedObject corner;
    corner.symbol = "WoodCorner";
    corner.col = 1;
    corner.row = 1;
    for (corner.rotation = 0; corner.rotation < 4; ++corner.rotation)
        CHECK(fence_piece_lean(g, corner) == glm::vec2(0.5f, 0.0f));
}
