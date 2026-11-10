// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path/decks.hpp"

#include <doctest/doctest.h>
#include <format>

using namespace rct3;
using namespace rct3::park;
using namespace rct3::park::path;

namespace
{
    save::model::TerrainGrid flat_grid(int width, int height)
    {
        save::model::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(width * height));
        return g;
    }

    /// A raised path at `height` drawn by object `id + 100`.
    save::model::PathTile raised(int col, int row, std::int32_t height, std::uint64_t id, std::uint8_t slope = 0, Side heading = Side::kNextRow)
    {
        save::model::PathTile p;
        p.id = id;
        p.kind = save::model::PathKind::Flying;
        p.col = static_cast<std::uint8_t>(col);
        p.row = static_cast<std::uint8_t>(row);
        p.direction = stored_direction(heading);
        p.surface_name = "Gravel";
        p.elevation = save::model::PathElevation { };
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
            s.pieces.push_back(std::format("Gravel_{}", i));
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
    CHECK(slope_deck(1, Side { 0 }, false, false) == Deck { DeckPiece::Slope, 3 });
    CHECK(slope_deck(2, Side { 0 }, false, false) == Deck { DeckPiece::Slope, 1 });
    CHECK(slope_deck(4, Side { 1 }, false, false) == Deck { DeckPiece::SlopeStraight, 0 });
    CHECK(slope_deck(4, Side { 0 }, true, false) == Deck { DeckPiece::SlopeStraightRight, 3 });
    CHECK(slope_deck(4, Side { 0 }, false, true) == Deck { DeckPiece::SlopeStraightLeft, 3 });
    CHECK(slope_deck(5, Side { 0 }, true, false) == Deck { DeckPiece::SlopeStraightLeft, 1 });
    CHECK(slope_deck(5, Side { 0 }, true, true) == Deck { DeckPiece::SlopeMid, 1 });
    // Steep slopes have no wide pieces.
    CHECK(slope_deck(2, Side { 3 }, true, true) == Deck { DeckPiece::Slope, 0 });
}

TEST_CASE("headings and stored directions swap both ways")
{
    for (const Side h : kSides)
        CHECK(stored_heading(stored_direction(h)) == h);
    CHECK(stored_direction(Side::kNextRow) == 3);
    CHECK_FALSE(stored_heading(save::model::kNoDirection));
    // A bare count past 3 is taken as itself, as before headings were checked.
    CHECK(stored_heading_or_bare(save::model::kNoDirection) == Side::kColBefore);
    CHECK(stored_heading_or_bare(2) == Side::kRowBefore);
}

TEST_CASE("a path meets the edges of its tile at its own height")
{
    save::model::TerrainGrid g = flat_grid(3, 3);
    g.cells[1 * 3 + 1].corners = { 1, 1, 3, 3 }; // Higher towards higher rows
    save::model::PathTile ground;
    ground.kind = save::model::PathKind::Ground;
    ground.col = 1;
    ground.row = 1;
    CHECK(edge_height(ground, g, Side { 0 }) == doctest::Approx(3));
    CHECK(edge_height(ground, g, Side { 1 }) == doctest::Approx(2));

    // A gentle slope climbing towards higher columns reaches only its two ends.
    const save::model::PathTile slope = raised(1, 1, 4, 1, 4, Side::kNextCol);
    CHECK(edge_height(slope, g, Side { 3 }) == doctest::Approx(4));
    CHECK(edge_height(slope, g, Side { 1 }) == doctest::Approx(5));
    CHECK_FALSE(edge_height(slope, g, Side { 0 }));
    const save::model::PathTile down = raised(1, 1, 4, 1, 2, Side::kNextCol);
    CHECK(edge_height(down, g, Side { 1 }) == doctest::Approx(2));

    save::model::PathTile queue = raised(1, 1, 4, 1);
    queue.kind = save::model::PathKind::Queue;
    queue.queue = save::model::PathQueueInfo { };
    queue.queue->start_direction = 0; // Towards lower columns
    queue.queue->end_direction = 1;
    CHECK(edge_height(queue, g, Side { 3 }) == doctest::Approx(4));
    CHECK_FALSE(edge_height(queue, g, Side { 0 }));
}

TEST_CASE("raised paths take the pieces their neighbours call for")
{
    const save::model::TerrainGrid g = flat_grid(5, 5);
    // A row of three at height 2.
    std::vector<save::model::PathTile> paths { raised(1, 2, 2, 1), raised(2, 2, 2, 2), raised(3, 2, 2, 3) };
    std::vector<save::model::PlacedObject> objects;
    for (const save::model::PathTile& p : paths)
    {
        save::model::PlacedObject& o = objects.emplace_back();
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
        save::model::PlacedObject& o = objects.emplace_back();
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
    const save::model::TerrainGrid g = flat_grid(5, 5);
    const std::vector<save::model::PathTile> paths { raised(1, 2, 0, 1, 4), raised(2, 2, 0, 2, 4), raised(3, 2, 0, 3, 4) };
    CHECK(choose_deck(paths[0], paths, g).piece == DeckPiece::SlopeStraightLeft);
    CHECK(choose_deck(paths[1], paths, g).piece == DeckPiece::SlopeMid);
    CHECK(choose_deck(paths[2], paths, g).piece == DeckPiece::SlopeStraightRight);
}

TEST_CASE("a raised queue's piece follows its line")
{
    save::model::PathTile q = raised(1, 1, 2, 1);
    q.kind = save::model::PathKind::Queue;
    q.queue = save::model::PathQueueInfo { };
    const auto line = [&](Side start, Side end) {
        q.queue->start_direction = stored_direction(start);
        q.queue->end_direction = stored_direction(end);
        return queue_deck(q);
    };
    CHECK(line(Side { 0 }, Side { 2 }) == std::pair { QueuePiece::Straight, ModelTurn { 1 } });
    CHECK(line(Side { 3 }, Side { 1 }) == std::pair { QueuePiece::Straight, ModelTurn { 0 } });
    CHECK(line(Side { 2 }, Side { 3 }) == std::pair { QueuePiece::TurnLeft, ModelTurn { 3 } });
    CHECK(line(Side { 3 }, Side { 2 }) == std::pair { QueuePiece::TurnRight, ModelTurn { 0 } });

    q.elevation->slope = 1; // Steep up, heading +row
    CHECK(queue_deck(q) == std::pair { QueuePiece::SlopeUp, ModelTurn { 3 } });
    q.elevation->slope = 2;
    CHECK(queue_deck(q).first == QueuePiece::SlopeDown);
    q.elevation->slope = 5;
    CHECK(queue_deck(q) == std::pair { QueuePiece::SlopeStraight, ModelTurn { 1 } });

    PathStyle style;
    style.queue = true;
    style.pieces = { "Q_Straight", "Q_TurnL", "Q_TurnR", "Q_Up", "Q_Down", "Q_Gentle", "Q_Gentle" };
    CHECK(deck_for(style, q, std::span(&q, 1), flat_grid(3, 3)).first == "Q_Gentle");
}
