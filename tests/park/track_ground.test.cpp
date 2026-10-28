// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_ground.hpp"

#include "park/item_category.hpp"

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

    /// A piece of track on tile (2, 1) at height 0, its one square solid from level 0 to 4.
    std::pair<std::vector<save::PlacedObject>, ObjectModels> one_piece()
    {
        ObjectModel model;
        model.category = std::to_underlying(ItemCategory::TrackPiece);
        model.ground_height = false;
        model.solid = { std::array<std::int32_t, 2> { 0, 4 } };
        model.levels = { { 100, { }, { } } };
        save::PlacedObject o;
        o.id = 1;
        o.symbol = "Piece";
        o.col = 2;
        o.row = 1;
        o.base_height = 0;
        ObjectModels models;
        models.emplace(model_key(o), model);
        return { { o }, models };
    }
} // namespace

TEST_CASE("a piece passing into the ground cuts its tile's corners down to its floor, not over it or in a tunnel")
{
    save::TerrainGrid g = owned_grid(5, 3);
    auto [pieces, models] = one_piece();
    CHECK(ground_cuts(pieces, models, g).empty());
    g.at(2, 1).corners = { 0, 0, 1, 2 };
    auto cuts = ground_cuts(pieces, models, g);
    REQUIRE(cuts.size() == 1);
    CHECK(cuts[0].tile == save::Tile { 2, 1 });
    CHECK(cuts[0].corners == std::array { 0.0f, 0.0f, 0.0f, 0.0f });
    pieces[0].base_height = 1;
    cuts = ground_cuts(pieces, models, g);
    REQUIRE(cuts.size() == 1);
    CHECK(cuts[0].corners == std::array { 0.0f, 0.0f, 1.0f, 1.0f }); // Ground under the floor stays
    pieces[0].base_height = 2;
    CHECK(ground_cuts(pieces, models, g).empty());
    pieces[0].base_height = 0;
    g.at(2, 1).corners.fill(4 + kGroundCover); // Deep enough for a tunnel
    CHECK(ground_cuts(pieces, models, g).empty());
}

TEST_CASE("a cut is refused off land the park owns outright and where the land tools may not move the ground")
{
    save::TerrainGrid g = owned_grid(5, 3);
    const auto [pieces, models] = one_piece();
    g.at(2, 1).corners = { 0, 0, 1, 2 };
    const auto cuts = ground_cuts(pieces, models, g);
    GroundHolds holds;
    CHECK_FALSE(cuts_refused(cuts, g, holds, { }));
    holds.held.insert(1U << 16 | 2U);
    CHECK(cuts_refused(cuts, g, holds, { }));
    g.at(2, 1).flags = 0;
    CHECK(cuts_refused(cuts, g, { }, { }));
}
