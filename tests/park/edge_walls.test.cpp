// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/edge_walls.hpp"

#include "park/ground_paths.hpp"
#include "park/item_category.hpp"
#include "park/object_placement.hpp"
#include "park/path_decks.hpp"

#include <doctest/doctest.h>
#include <format>
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
        p.surface_name = "Gravel";
        p.col = static_cast<std::uint8_t>(col);
        p.row = static_cast<std::uint8_t>(row);
        return p;
    }

    /// A fence on tile (2, 1) whose +X faces after `turns` quarter turns, and its model, of `category`.
    std::pair<save::PlacedObject, ObjectModels> fence(std::int32_t turns, ItemCategory category = ItemCategory::Fence, Positioning positioning = Positioning::Edge)
    {
        ObjectModel model;
        model.category = std::to_underlying(category);
        model.positioning = std::to_underlying(positioning);
        model.levels = { { 100, { }, { } } };
        save::PlacedObject o;
        o.symbol = "Fence";
        o.col = 2;
        o.row = 1;
        o.rotation = turns;
        o.base_height = 0;
        ObjectModels models;
        models.emplace(model_key(o), model);
        return { o, models };
    }
} // namespace

TEST_CASE("a fence stands along the side its +X faces, listed on both tiles it parts")
{
    const save::TerrainGrid grid = flat_grid(5, 5);
    const auto [o, models] = fence(2); // +X
    const auto wall = edge_wall(o, models, grid);
    REQUIRE(wall);
    CHECK(wall->side == 1);
    const EdgeWalls walls = edge_walls(std::vector { o }, models, grid);
    CHECK(walled(walls, { 2, 1 }, 1, 0));
    CHECK(walled(walls, { 3, 1 }, 3, 0));
    CHECK_FALSE(walled(walls, { 2, 1 }, 0, 0));
    CHECK_FALSE(walled(walls, { 2, 1 }, 1, 6)); // A deck high over it
    // Neither a fence item set elsewhere than an edge, nor a stall, nor a hidden fence is a wall.
    const auto [lamp, lamp_models] = fence(2, ItemCategory::Fence, Positioning::PathSide);
    CHECK_FALSE(edge_wall(lamp, lamp_models, grid));
    const auto [stall, stall_models] = fence(2, ItemCategory::Stall);
    CHECK_FALSE(edge_wall(stall, stall_models, grid));
    save::PlacedObject gone = o;
    gone.hidden = true;
    CHECK_FALSE(edge_wall(gone, models, grid));
}

TEST_CASE("paths on the ground and on decks do not join across a wall")
{
    const save::TerrainGrid grid = flat_grid(5, 5);
    const auto [o, models] = fence(2);
    const EdgeWalls walls = edge_walls(std::vector { o }, models, grid);
    const std::vector<save::PathTile> line { ground(1, 1), ground(2, 1), ground(3, 1) };
    GroundPathMap map = ground_path_map(line, grid);
    CHECK(path_continues(map, grid, { 2, 1 }, 1, 0));
    map.walls = walls;
    CHECK_FALSE(path_continues(map, grid, { 2, 1 }, 1, 0));
    CHECK_FALSE(path_continues(map, grid, { 3, 1 }, -1, 0));
    CHECK(path_continues(map, grid, { 2, 1 }, -1, 0));

    std::vector<save::PathTile> decks;
    for (int col = 1; col <= 3; ++col)
    {
        save::PathTile p = ground(col, 1);
        p.kind = save::PathKind::Flying;
        p.elevation = save::PathElevation { };
        decks.push_back(p);
    }
    // Joined both ways, a straight; parted on +X, an end.
    CHECK(choose_deck(decks[1], decks, grid).piece == DeckPiece::StraightA);
    CHECK(choose_deck(decks[1], decks, grid, walls).piece == DeckPiece::TurnU);
}
