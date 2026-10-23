// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path_furniture.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid flat_grid(int width, int height)
    {
        save::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(width * height));
        return g;
    }

    save::PathTile ground(int col, int row)
    {
        save::PathTile p;
        p.kind = save::PathKind::Ground;
        p.col = static_cast<std::uint8_t>(col);
        p.row = static_cast<std::uint8_t>(row);
        return p;
    }

    /// The world point at column and row with fractions, `y` high.
    glm::vec3 at(float col, float row, float y = 0)
    {
        return { col * 4, y, row * 4 };
    }

    /// A path along row 1 from column 1 to 3.
    const std::vector<save::PathTile> kLine { ground(1, 1), ground(2, 1), ground(3, 1) };
} // namespace

TEST_CASE("a bench goes against the free side of a path nearest the pointer")
{
    const save::TerrainGrid grid = flat_grid(8, 8);
    // Nearest the joined side towards column 3, it takes the nearer of the free sides.
    const auto spot = furniture_spot(kLine, grid, at(2.9f, 1.6f), Positioning::PathSide, 0);
    REQUIRE(spot);
    CHECK(spot->tile == save::Tile { 2, 1 });
    CHECK(faced_side(spot->turns) == 0); // Towards higher rows
    CHECK_FALSE(spot->raised);
    const auto other = furniture_spot(kLine, grid, at(2.5f, 1.1f), Positioning::PathSide, 0);
    REQUIRE(other);
    CHECK(faced_side(other->turns) == 2);
    // At the line's end the end itself is free.
    CHECK(faced_side(furniture_spot(kLine, grid, at(3.95f, 1.5f), Positioning::PathSide, 0)->turns) == 1);
}

TEST_CASE("a sign where paths join takes a joined side, and the middle keeps the player's turn")
{
    const save::TerrainGrid grid = flat_grid(8, 8);
    CHECK(faced_side(furniture_spot(kLine, grid, at(2.9f, 1.6f), Positioning::PathSideJoin, 0)->turns) == 1);
    CHECK(faced_side(furniture_spot(kLine, grid, at(2.1f, 1.6f), Positioning::PathSideJoin, 0)->turns) == 3);
    CHECK(furniture_spot(kLine, grid, at(2.9f, 1.6f), Positioning::PathCentre, 3)->turns == 3);
}

TEST_CASE("off the paths nothing goes, in a plaza the nearest side, on a deck its height")
{
    const save::TerrainGrid grid = flat_grid(8, 8);
    CHECK_FALSE(furniture_spot(kLine, grid, at(5.5f, 5.5f), Positioning::PathSide, 0));
    CHECK_FALSE(furniture_spot(kLine, grid, at(-1, 1.5f), Positioning::PathSide, 0));

    std::vector<save::PathTile> plaza;
    for (int col = 1; col <= 3; ++col)
        for (int row = 1; row <= 3; ++row)
            plaza.push_back(ground(col, row));
    CHECK(faced_side(furniture_spot(plaza, grid, at(2.9f, 2.5f), Positioning::PathSide, 0)->turns) == 1);

    // A deck over the ground path on the same tile, taken when the pointer is up at it.
    std::vector<save::PathTile> stacked = kLine;
    save::PathTile deck = ground(2, 1);
    deck.kind = save::PathKind::Flying;
    deck.elevation = save::PathElevation { };
    deck.elevation->quantised_height = 6;
    stacked.push_back(deck);
    const auto up = furniture_spot(stacked, grid, at(2.5f, 1.9f, 5.5f), Positioning::PathSide, 0);
    REQUIRE(up);
    CHECK(up->raised);
    CHECK(up->height == 6);
    CHECK_FALSE(furniture_spot(stacked, grid, at(2.5f, 1.9f, 0.2f), Positioning::PathSide, 0)->raised);
}

TEST_CASE("one item stands against each side of a tile, and one in its middle")
{
    ObjectModels models;
    models["Style|Bench"].positioning = std::to_underlying(Positioning::PathSide);
    models["Style|Fountain"].positioning = std::to_underlying(Positioning::PathCentre);
    models["Style|Tree"].positioning = std::to_underlying(Positioning::Tile);
    const auto placed = [](std::string symbol, std::int32_t rotation, std::int32_t height = 0) {
        save::PlacedObject o;
        o.overlay = "Style";
        o.symbol = std::move(symbol);
        o.col = 2;
        o.row = 1;
        o.rotation = rotation;
        o.base_height = height;
        return o;
    };
    std::vector<save::PlacedObject> objects { placed("Bench", 1), placed("Fountain", 0), placed("Tree", 2) };
    CHECK(furniture_taken(objects, models, placed("Bench", 1), Positioning::PathSide));
    CHECK_FALSE(furniture_taken(objects, models, placed("Bench", 3), Positioning::PathSide));
    // The tree is no path item, and the fountain takes only the middle.
    CHECK_FALSE(furniture_taken(objects, models, placed("Bench", 2), Positioning::PathSide));
    CHECK(furniture_taken(objects, models, placed("Fountain", 2), Positioning::PathCentre));
    // On a deck far above, the side is free; once taken away, free again.
    CHECK_FALSE(furniture_taken(objects, models, placed("Bench", 1, 8), Positioning::PathSide));
    objects[0].hidden = true;
    CHECK_FALSE(furniture_taken(objects, models, placed("Bench", 1), Positioning::PathSide));
}
