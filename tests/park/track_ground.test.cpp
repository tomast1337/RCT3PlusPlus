// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_ground.hpp"

#include "park/item_category.hpp"

#include <cmath>
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
    CHECK(ground_cuts(pieces, models, g, false).footprint.empty());
    g.at(2, 1).corners = { 0, 0, 1, 2 };
    auto cuts = ground_cuts(pieces, models, g, false).footprint;
    REQUIRE(cuts.size() == 1);
    CHECK(cuts[0].tile == save::Tile { 2, 1 });
    CHECK(cuts[0].corners == std::array { 0.0f, 0.0f, 0.0f, 0.0f });
    pieces[0].base_height = 1;
    cuts = ground_cuts(pieces, models, g, false).footprint;
    REQUIRE(cuts.size() == 1);
    CHECK(cuts[0].corners == std::array { 0.0f, 0.0f, 1.0f, 1.0f }); // Ground under the floor stays
    pieces[0].base_height = 2;
    CHECK(ground_cuts(pieces, models, g, false).footprint.empty());
    pieces[0].base_height = 0;
    g.at(2, 1).corners.fill(4 + kGroundCover); // Deep enough for a tunnel
    CHECK(ground_cuts(pieces, models, g, false).footprint.empty());
}

TEST_CASE("a cut is refused off land the park owns outright and where the land tools may not move the ground")
{
    save::TerrainGrid g = owned_grid(5, 3);
    const auto [pieces, models] = one_piece();
    g.at(2, 1).corners = { 0, 0, 1, 2 };
    const auto cuts = ground_cuts(pieces, models, g, false).footprint;
    GroundHolds holds;
    CHECK_FALSE(cuts_refused(cuts, g, holds, { }));
    GroundCuts all { .footprint = cuts, .rim = { } };
    CHECK(allow_cuts(all, g, holds, { }));
    holds.held.insert(1U << 16 | 2U);
    CHECK(cuts_refused(cuts, g, holds, { }));
    CHECK_FALSE(allow_cuts(all, g, holds, { }));
    CHECK(all.footprint.empty());
    g.at(2, 1).flags = 0;
    CHECK(cuts_refused(cuts, g, { }, { }));
}

TEST_CASE("a smooth cut slopes the tiles round it down to meet it, leaving the far side of each as it was")
{
    save::TerrainGrid g = owned_grid(5, 3);
    auto [pieces, models] = one_piece();
    for (save::TerrainCell& c : g.cells)
        c.corners.fill(2);
    const GroundCuts sharp = ground_cuts(pieces, models, g, false);
    CHECK(sharp.footprint.size() == 1);
    CHECK(sharp.rim.empty());
    const GroundCuts smooth = ground_cuts(pieces, models, g, true);
    REQUIRE(smooth.footprint.size() == 1);
    REQUIRE(smooth.rim.size() == 8);
    for (const GroundCut& cut : smooth.rim)
    {
        if (cut.tile == save::Tile { 3, 1 }) // East: its west corners meet the cut
            CHECK(cut.corners == std::array { 0.0f, 2.0f, 0.0f, 2.0f });
        if (cut.tile == save::Tile { 1, 0 }) // North-west: only the corner it shares
            CHECK(cut.corners == std::array { 2.0f, 2.0f, 2.0f, 0.0f });
    }
    CHECK(cuts_cost(smooth, g) == std::lround(4 * 2 * kShapeCost + 4 * 2 * 2 * kShapeCost + 4 * 2 * kShapeCost));
}

TEST_CASE("a piece, ride or stall under enough ground all round hollows its tile up to its top, scenery never does")
{
    save::TerrainGrid g = owned_grid(5, 3);
    auto [pieces, models] = one_piece();
    g.at(2, 1).corners.fill(4 + kGroundCover);
    const auto hollows = buried_hollows(pieces, models, g);
    REQUIRE(hollows.size() == 1);
    CHECK(hollows[0].col == 2);
    CHECK(hollows[0].row == 1);
    CHECK(hollows[0].low < 0);
    CHECK(hollows[0].high == doctest::Approx(4));
    g.at(2, 1).corners[0] = 4.5f;
    CHECK(buried_hollows(pieces, models, g).empty());
    g.at(2, 1).corners.fill(4 + kGroundCover);
    models.begin()->second.category = std::to_underlying(ItemCategory::Stall);
    CHECK(buried_hollows(pieces, models, g).size() == 1);
    models.begin()->second.category = std::to_underlying(ItemCategory::Ride);
    CHECK(buried_hollows(pieces, models, g).size() == 1);
    models.begin()->second.category = std::to_underlying(ItemCategory::SmallScenery);
    CHECK(buried_hollows(pieces, models, g).empty());
}

TEST_CASE("an item lies buried only under enough ground on every tile it fills")
{
    save::TerrainGrid g = owned_grid(5, 3);
    auto [pieces, models] = one_piece();
    CHECK_FALSE(lies_buried(pieces, models, g));
    g.at(2, 1).corners.fill(4 + kGroundCover);
    CHECK(lies_buried(pieces, models, g));
    // Partly buried, its top in the ground.
    g.at(2, 1).corners[3] = 4.5f;
    CHECK_FALSE(lies_buried(pieces, models, g));
    // Two squares, one of them under too little ground.
    g.at(2, 1).corners.fill(4 + kGroundCover);
    models.begin()->second.solid.push_back(std::array<std::int32_t, 2> { 0, 4 });
    models.begin()->second.squares = { 1, 2 };
    CHECK_FALSE(lies_buried(pieces, models, g));
    for (save::TerrainCell& c : g.cells)
        c.corners.fill(4 + kGroundCover);
    CHECK(lies_buried(pieces, models, g));
}
