// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/scenery_fit.hpp"

#include "core/types.hpp"
#include "park/item_category.hpp"

#include <cstdint>
#include <doctest/doctest.h>
#include <utility>
#include <vector>

using namespace rct3;
using namespace rct3::park;

namespace
{
    /// A grid of 5 by 3 tiles, owned but for the last column.
    save::model::TerrainGrid grid()
    {
        save::model::TerrainGrid g;
        g.width = 5;
        g.height = 3;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(15);
        for (i32 row = 0; row < 3; ++row)
            for (i32 col = 0; col < 4; ++col)
                g.cells[static_cast<usize>(row * 5 + col)].flags = static_cast<u8>(save::model::TileFlag::Owned);
        return g;
    }

    save::model::PlacedObject item(const char* symbol, i32 col, i32 row)
    {
        save::model::PlacedObject o;
        o.symbol = symbol;
        o.col = col;
        o.row = row;
        o.base_height = 0;
        return o;
    }

    /// A solid tree (a prop) and a solid piece of track, each filling one tile up to 4.
    object::ObjectModels models()
    {
        object::ObjectModels m;
        for (const auto [symbol, category] : { std::pair { "tree", ItemCategory::Tree }, std::pair { "track", ItemCategory::TrackPiece } })
        {
            object::ObjectModel model;
            model.category = std::to_underlying(category);
            model.ground_height = false;
            model.solid = { std::array<i32, 2> { 0, 4 } };
            model.levels = { { 100, { }, { } } };
            m.emplace(object::model_key(item(symbol, 0, 0)), model);
        }
        return m;
    }
} // namespace

TEST_CASE("in a park scenery stands only on owned land, clear of solid items and paths")
{
    const save::model::TerrainGrid g = grid();
    const object::ObjectModels m = models();
    const std::vector<save::model::PlacedObject> standing { item("tree", 1, 1), item("track", 2, 1) };
    const path::SolidTiles room = scenery_room(standing, m, g, FitRules::Park);
    const std::vector<save::model::PathTile> paths { { .kind = save::model::PathKind::Ground, .col = 3, .row = 1 } };
    const auto problem = [&](i32 col, i32 row) {
        const save::model::PlacedObject tree = item("tree", col, row);
        return scenery_problem(std::span(&tree, 1), m, room, paths, g, FitRules::Park);
    };
    CHECK(problem(0, 0) == ride::SiteProblem::None);
    CHECK(problem(4, 0) == ride::SiteProblem::NotOwned);
    CHECK(problem(1, 1) == ride::SiteProblem::Blocked);
    CHECK(problem(2, 1) == ride::SiteProblem::Blocked);
    CHECK(problem(3, 1) == ride::SiteProblem::Path);
}

TEST_CASE("in a sandbox scenery stands anywhere on the map, through track and paths but not other scenery")
{
    const save::model::TerrainGrid g = grid();
    const object::ObjectModels m = models();
    const std::vector<save::model::PlacedObject> standing { item("tree", 1, 1), item("track", 2, 1) };
    const path::SolidTiles room = scenery_room(standing, m, g, FitRules::Sandbox);
    const std::vector<save::model::PathTile> paths { { .kind = save::model::PathKind::Ground, .col = 3, .row = 1 } };
    const auto problem = [&](i32 col, i32 row) {
        const save::model::PlacedObject tree = item("tree", col, row);
        return scenery_problem(std::span(&tree, 1), m, room, paths, g, FitRules::Sandbox);
    };
    CHECK(problem(4, 0) == ride::SiteProblem::None);
    CHECK(problem(5, 0) == ride::SiteProblem::NotOwned);
    CHECK(problem(1, 1) == ride::SiteProblem::Blocked);
    CHECK(problem(2, 1) == ride::SiteProblem::None);
    CHECK(problem(3, 1) == ride::SiteProblem::None);
}
