// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path_decks.hpp"

#include <doctest/doctest.h>

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

    /// A raised path at `height` drawn by object `id + 100`.
    save::PathTile raised(int col, int row, std::int32_t height, std::uint64_t id, std::uint8_t slope = 0, std::uint8_t heading = 0)
    {
        save::PathTile p;
        p.id = id;
        p.kind = save::PathKind::Flying;
        p.col = static_cast<std::uint8_t>(col);
        p.row = static_cast<std::uint8_t>(row);
        p.direction = stored_direction(heading);
        p.surface_name = "Gravel";
        p.elevation = save::PathElevation { };
        p.elevation->slope = slope;
        p.elevation->quantised_height = height;
        p.elevation->scenery_ref = id + 100;
        return p;
    }

    PathStyle gravel()
    {
        PathStyle s;
        s.name = "Gravel";
        for (std::size_t i = 0; i < kDeckPieceCount; ++i)
            s.pieces.push_back("Gravel_" + std::to_string(i));
        return s;
    }
} // namespace

TEST_CASE("a flat tile's piece follows its joined sides and filled corners")
{
    CHECK(flat_deck(0b0000, 0) == Deck { DeckPiece::Flat, 0 });
    CHECK(flat_deck(0b0010, 0) == Deck { DeckPiece::TurnU, 0 });
    CHECK(flat_deck(0b1000, 0) == Deck { DeckPiece::TurnU, 2 });
    CHECK(flat_deck(0b1010, 0) == Deck { DeckPiece::StraightA, 0 });
    // A straight piece fits two turns; the smaller is taken.
    CHECK(flat_deck(0b0101, 0) == Deck { DeckPiece::StraightA, 1 });
    CHECK(flat_deck(0b1100, 0) == Deck { DeckPiece::TurnLA, 1 });
    CHECK(flat_deck(0b1100, 0b0100) == Deck { DeckPiece::TurnLB, 1 });
    CHECK(flat_deck(0b1011, 0) == Deck { DeckPiece::TurnTA, 0 });
    CHECK(flat_deck(0b1011, 0b1001) == Deck { DeckPiece::StraightB, 0 });
    CHECK(flat_deck(0b0111, 0b0011) == Deck { DeckPiece::StraightB, 1 });
    CHECK(flat_deck(0b1111, 0) == Deck { DeckPiece::TurnX, 0 });
    CHECK(flat_deck(0b1111, 0b0100) == Deck { DeckPiece::CornerD, 1 });
    CHECK(flat_deck(0b1111, 0b0111) == Deck { DeckPiece::CornerA, 2 });
    CHECK(flat_deck(0b1111, 0b1111) == Deck { DeckPiece::Flat, 0 });
    // A corner between sides that are not both joined is ignored.
    CHECK(flat_deck(0b0110, 0b1101) == flat_deck(0b0110, 0));
}

TEST_CASE("a sloping tile's piece follows its steepness, its way and the slopes beside it")
{
    CHECK(slope_deck(1, 0, false, false) == Deck { DeckPiece::Slope, 3 });
    CHECK(slope_deck(2, 0, false, false) == Deck { DeckPiece::Slope, 1 });
    CHECK(slope_deck(4, 1, false, false) == Deck { DeckPiece::SlopeStraight, 0 });
    CHECK(slope_deck(4, 0, true, false) == Deck { DeckPiece::SlopeStraightRight, 3 });
    CHECK(slope_deck(4, 0, false, true) == Deck { DeckPiece::SlopeStraightLeft, 3 });
    CHECK(slope_deck(5, 0, true, false) == Deck { DeckPiece::SlopeStraightLeft, 1 });
    CHECK(slope_deck(5, 0, true, true) == Deck { DeckPiece::SlopeMid, 1 });
    // Steep slopes have no wide pieces.
    CHECK(slope_deck(2, 3, true, true) == Deck { DeckPiece::Slope, 0 });
}

TEST_CASE("headings and stored directions swap both ways")
{
    for (std::uint8_t h = 0; h < 4; ++h)
        CHECK(stored_direction(stored_direction(h)) == h);
    CHECK(stored_direction(0) == 3);
    CHECK(stored_direction(255) == 255);
}

TEST_CASE("a path meets the edges of its tile at its own height")
{
    save::TerrainGrid g = flat_grid(3, 3);
    g.cells[1 * 3 + 1].corners = { 1, 1, 3, 3 }; // Higher towards higher rows
    save::PathTile ground;
    ground.kind = save::PathKind::Ground;
    ground.col = 1;
    ground.row = 1;
    CHECK(edge_height(ground, g, 0) == doctest::Approx(3));
    CHECK(edge_height(ground, g, 1) == doctest::Approx(2));

    // A gentle slope climbing towards higher columns reaches only its two ends.
    const save::PathTile slope = raised(1, 1, 4, 1, 4, 1);
    CHECK(edge_height(slope, g, 3) == doctest::Approx(4));
    CHECK(edge_height(slope, g, 1) == doctest::Approx(5));
    CHECK_FALSE(edge_height(slope, g, 0));
    const save::PathTile down = raised(1, 1, 4, 1, 2, 1);
    CHECK(edge_height(down, g, 1) == doctest::Approx(2));

    save::PathTile queue = raised(1, 1, 4, 1);
    queue.kind = save::PathKind::Queue;
    queue.queue = save::PathQueueInfo { };
    queue.queue->start_direction = 0; // Towards lower columns
    queue.queue->end_direction = 1;
    CHECK(edge_height(queue, g, 3) == doctest::Approx(4));
    CHECK_FALSE(edge_height(queue, g, 0));
}

TEST_CASE("raised paths take the pieces their neighbours call for")
{
    const save::TerrainGrid g = flat_grid(5, 5);
    // A row of three at height 2.
    std::vector<save::PathTile> paths { raised(1, 2, 2, 1), raised(2, 2, 2, 2), raised(3, 2, 2, 3) };
    std::vector<save::PlacedObject> objects;
    for (const save::PathTile& p : paths)
    {
        save::PlacedObject& o = objects.emplace_back();
        o.id = p.elevation->scenery_ref;
        o.symbol = "old";
    }
    const std::vector<PathStyle> styles { gravel() };
    CHECK(refresh_decks(paths, objects, g, styles, 0, 0, 4, 4) == 3);
    CHECK(objects[0].symbol == deck_symbol(styles[0], DeckPiece::TurnU));
    CHECK(objects[0].rotation == 0);
    CHECK(objects[1].symbol == deck_symbol(styles[0], DeckPiece::StraightA));
    CHECK(objects[2].rotation == 2);
    // Nothing more to change.
    CHECK(refresh_decks(paths, objects, g, styles, 0, 0, 4, 4) == 0);

    // A second row beside the first makes a wide path: edges along both.
    for (int col = 1; col <= 3; ++col)
        paths.push_back(raised(col, 3, 2, static_cast<std::uint64_t>(10 + col)));
    for (std::size_t i = 3; i < paths.size(); ++i)
    {
        save::PlacedObject& o = objects.emplace_back();
        o.id = paths[i].elevation->scenery_ref;
    }
    (void)refresh_decks(paths, objects, g, styles, 0, 0, 4, 4);
    CHECK(choose_deck(paths[1], paths, g) == Deck { DeckPiece::StraightB, 0 });
    CHECK(choose_deck(paths[0], paths, g) == Deck { DeckPiece::TurnLB, 3 });
    CHECK(objects[1].symbol == deck_symbol(styles[0], DeckPiece::StraightB));

    // A path one unit lower does not join.
    paths.push_back(raised(2, 1, 1, 20));
    CHECK(choose_deck(paths[1], paths, g) == Deck { DeckPiece::StraightB, 0 });
}

TEST_CASE("a gentle slope beside another takes the wide pieces")
{
    const save::TerrainGrid g = flat_grid(5, 5);
    const std::vector<save::PathTile> paths { raised(1, 2, 0, 1, 4, 0), raised(2, 2, 0, 2, 4, 0), raised(3, 2, 0, 3, 4, 0) };
    CHECK(choose_deck(paths[0], paths, g).piece == DeckPiece::SlopeStraightLeft);
    CHECK(choose_deck(paths[1], paths, g).piece == DeckPiece::SlopeMid);
    CHECK(choose_deck(paths[2], paths, g).piece == DeckPiece::SlopeStraightRight);
}
