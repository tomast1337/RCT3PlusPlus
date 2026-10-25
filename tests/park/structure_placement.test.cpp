// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/structure_placement.hpp"

#include "park/item_category.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <utility>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid owned_grid(int width, int height)
    {
        save::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(width * height));
        for (save::TerrainCell& c : g.cells)
            c.flags = static_cast<std::uint8_t>(save::TileFlag::Owned);
        return g;
    }

    save::PlacedObject item(const char* symbol, std::int32_t col, std::int32_t row, std::int32_t height)
    {
        save::PlacedObject o;
        o.symbol = symbol;
        o.overlay = "Scenery/Invented";
        o.col = col;
        o.row = row;
        o.base_height = height;
        o.force_absolute_height = true;
        return o;
    }

    /// Models for the invented items: a solid tree, a bench, and a wall two tiles long.
    ObjectModels models()
    {
        ObjectModels m;
        ObjectModel tree;
        tree.category = std::to_underlying(ItemCategory::Tree);
        tree.ground_height = false;
        tree.solid = { std::array<std::int32_t, 2> { 0, 4 } };
        tree.levels = { { 100, { }, { } } };
        m.emplace(model_key(item("tree", 0, 0, 0)), tree);
        ObjectModel bench;
        bench.category = std::to_underlying(ItemCategory::Bench);
        m.emplace(model_key(item("bench", 0, 0, 0)), bench);
        ObjectModel wall;
        wall.category = std::to_underlying(ItemCategory::Wall);
        wall.squares = { 2, 1 };
        m.emplace(model_key(item("wall", 0, 0, 0)), wall);
        return m;
    }
} // namespace

TEST_CASE("a structure turns about its middle tile, and four turns bring every tile back")
{
    const save::Structure s { .cols = 3, .rows = 2, .tiles = std::vector<std::uint8_t>(6, 1) };
    for (std::int32_t turns = 0; turns < 4; ++turns)
        CHECK(structure_tile(s, { 10, 10 }, turns, 1, 1) == save::Tile { 10, 10 });
    CHECK(structure_tile(s, { 10, 10 }, 0, 0, 0) == save::Tile { 9, 9 });
    // A quarter turn lays its columns along the park's rows, the first column last.
    CHECK(structure_tile(s, { 10, 10 }, 1, 0, 0) == save::Tile { 9, 11 });
    CHECK(structure_tile(s, { 10, 10 }, 1, 2, 0) == save::Tile { 9, 9 });
    CHECK(structure_tile(s, { 10, 10 }, 2, 0, 0) == save::Tile { 11, 11 });
}

TEST_CASE("picking takes the scenery on the marked tiles, its box round them, its heights from the lowest")
{
    const save::TerrainGrid g = owned_grid(8, 8);
    const ObjectModels m = models();
    std::vector<save::PlacedObject> objects { item("tree", 2, 2, 5), item("bench", 2, 2, 5), item("tree", 3, 3, 7), item("tree", 6, 6, 0) };
    objects[0].rotation = 2;
    objects[0].quadrant = 3;
    objects[0].colours = { 1, 2, 3 };
    const std::vector<save::Tile> marked { { 2, 2 }, { 3, 3 } };
    const save::Structure s = pick_structure(marked, objects, m, g);
    CHECK(s.cols == 2);
    CHECK(s.rows == 2);
    CHECK(s.tiles == std::vector<std::uint8_t> { 1, 0, 0, 1 });
    REQUIRE(s.pieces.size() == 2); // The bench stays with the path it would stand on
    CHECK(s.pieces[0].height == 0);
    CHECK(s.pieces[0].rotation == 2);
    CHECK(s.pieces[0].quadrant == 3);
    CHECK(s.pieces[0].colours == std::array<std::int32_t, 3> { 1, 2, 3 });
    CHECK(s.pieces[1].col == 1);
    CHECK(s.pieces[1].row == 1);
    CHECK(s.pieces[1].height == 2);
    CHECK(pick_structure({ }, objects, m, g).tiles.empty());
}

TEST_CASE("built turned, each item keeps its tiles, quarter and facing in the box, above the ground given")
{
    const ObjectModels m = models();
    const save::Structure s {
        .cols = 2,
        .rows = 1,
        .tiles = { 1, 1 },
        .pieces = { { .symbol = "wall", .overlay = "Scenery/Invented", .col = 0, .row = 0, .height = 0, .rotation = 0 }, { .symbol = "tree", .overlay = "Scenery/Invented", .col = 1, .row = 0, .height = 3, .rotation = 1, .quadrant = 1 } },
    };
    const std::vector<save::PlacedObject> placed = structure_objects(s, { 5, 5 }, 1, 10, m);
    REQUIRE(placed.size() == 2);
    // The wall, two tiles along the box, now runs along the park's rows over the box's turned tiles.
    const save::Tile first = structure_tile(s, { 5, 5 }, 1, 0, 0);
    const save::Tile second = structure_tile(s, { 5, 5 }, 1, 1, 0);
    CHECK(placed[0].col == std::min(first.col, second.col));
    CHECK(placed[0].row == std::min(first.row, second.row));
    CHECK(first.col == second.col);
    CHECK(placed[0].rotation == 1);
    CHECK(placed[0].base_height == 10);
    CHECK(placed[1].col == second.col);
    CHECK(placed[1].row == second.row);
    CHECK(placed[1].rotation == 2);
    CHECK(placed[1].base_height == 13);
    CHECK(placed[1].quadrant == 0); // The +X quarter of the tile turns to its -Z quarter
    // Turned all the way round, the same items come back.
    const auto again = structure_objects(s, { 5, 5 }, 4, 10, m);
    const auto plain = structure_objects(s, { 5, 5 }, 0, 10, m);
    for (std::size_t i = 0; i < plain.size(); ++i)
    {
        CHECK(again[i].col == plain[i].col);
        CHECK(again[i].row == plain[i].row);
        CHECK(again[i].quadrant == plain[i].quadrant);
    }
}

TEST_CASE("a structure fits on the park's own land, clear of solid items and paths")
{
    save::TerrainGrid g = owned_grid(8, 8);
    const ObjectModels m = models();
    const save::Structure s { .cols = 1, .rows = 1, .tiles = { 1 }, .pieces = { { .symbol = "tree", .overlay = "Scenery/Invented" } } };
    const auto at = [&](save::Tile t) { return structure_objects(s, t, 0, 0, m); };
    CHECK(structure_problem(s, { 3, 3 }, 0, at({ 3, 3 }), m, { }, { }, g) == StructureProblem::None);
    CHECK(structure_problem(s, { 9, 3 }, 0, at({ 9, 3 }), m, { }, { }, g) == StructureProblem::NotOwned);
    g.cells[3 * 8 + 4].flags = 0;
    CHECK(structure_problem(s, { 4, 3 }, 0, at({ 4, 3 }), m, { }, { }, g) == StructureProblem::NotOwned);
    const std::vector<save::PlacedObject> standing { item("tree", 3, 3, 0) };
    CHECK(structure_problem(s, { 3, 3 }, 0, at({ 3, 3 }), m, solid_tiles(standing, m, g), { }, g) == StructureProblem::Blocked);
    CHECK(structure_problem(s, { 3, 3 }, 0, structure_objects(s, { 3, 3 }, 0, 8, m), m, solid_tiles(standing, m, g), { }, g) == StructureProblem::None);
}
