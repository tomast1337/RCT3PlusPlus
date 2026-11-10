// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track/edit.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3;
using namespace rct3::park;
using namespace rct3::park::track;

namespace
{
    constexpr float kTile = 4;

    /// A track of pieces with ids 1 to `count`, in stretches of the sizes `stretches` gives, closed as they say.
    save::model::Track stretched_track(std::vector<std::pair<std::size_t, bool>> stretches)
    {
        save::model::Track t;
        for (const auto& [count, closed] : stretches)
        {
            t.segments.push_back({ .first_piece = t.pieces.size(), .piece_count = count, .closed = closed });
            for (std::size_t i = 0; i < count; ++i)
                t.pieces.push_back({ .id = t.pieces.size() + 1 });
        }
        return t;
    }

    std::vector<save::model::ObjectId> ids(const save::model::Track& t)
    {
        std::vector<save::model::ObjectId> out;
        for (const save::model::TrackPiece& p : t.pieces)
            out.push_back(p.id);
        return out;
    }

    save::model::TerrainGrid flat()
    {
        save::model::TerrainGrid g;
        g.width = 10;
        g.height = 10;
        g.tile_size_x = kTile;
        g.tile_size_z = kTile;
        g.cells.resize(100);
        return g;
    }

    save::model::PlacedObject placed(int col, int row)
    {
        save::model::PlacedObject o;
        o.col = col;
        o.row = row;
        o.base_height = 3;
        o.force_absolute_height = true;
        return o;
    }

    /// A level straight one tile long, along the model's -X.
    PieceShape straight(std::string end = "")
    {
        PieceRecord r;
        r.name = "Straight";
        r.item = "StraightItem";
        r.joins[1].name = std::move(end);
        PieceLine l;
        l.start = { kTile / 2, 0, 0 };
        l.end = { -kTile / 2, 0, 0 };
        return shape_piece(std::move(r), l);
    }
} // namespace

TEST_CASE("a piece has neighbours unless it is at an open end, round the ends of a closed stretch")
{
    const save::model::Track t = stretched_track({ { 4, false }, { 3, true } });
    CHECK_FALSE(piece_neighbours(t, 0));
    CHECK_FALSE(piece_neighbours(t, 3));
    REQUIRE(piece_neighbours(t, 1));
    CHECK(piece_neighbours(t, 1)->before == 0);
    CHECK(piece_neighbours(t, 1)->after == 2);
    // The second stretch closes on itself.
    REQUIRE(piece_neighbours(t, 4));
    CHECK(piece_neighbours(t, 4)->before == 6);
    CHECK(piece_neighbours(t, 6)->after == 4);
    CHECK_FALSE(piece_neighbours(t, 7));
    // A track that names no stretches is one, closed when it is a circuit.
    save::model::Track whole = stretched_track({ { 3, false } });
    whole.segments.clear();
    whole.circuit = true;
    CHECK(piece_neighbours(whole, 0));
}

TEST_CASE("a piece taken out of a closed stretch opens it there, building on from the piece before")
{
    save::model::Track t = stretched_track({ { 5, true } });
    t.circuit = true;
    CHECK(take_out_piece(t, 1).id == 2);
    CHECK(ids(t) == std::vector<save::model::ObjectId> { 3, 4, 5, 1 });
    REQUIRE(t.segments.size() == 1);
    CHECK_FALSE(t.segments[0].closed);
    CHECK(t.segments[0].piece_count == 4);
    CHECK_FALSE(t.circuit);
}

TEST_CASE("a piece taken out of an open stretch splits it, the pieces before it last, and the halves join again")
{
    save::model::Track t = stretched_track({ { 2, true }, { 5, false } });
    CHECK(take_out_piece(t, 4).id == 5);
    CHECK(ids(t) == std::vector<save::model::ObjectId> { 1, 2, 6, 7, 3, 4 });
    REQUIRE(t.segments.size() == 3);
    CHECK(t.segments[1].first_piece == 2);
    CHECK(t.segments[2].first_piece == 4);
    CHECK(t.segments[2].piece_count == 2);
    CHECK(t.pieces[5].segment == 2);
    // Built across the gap, the stretch after it joins on.
    join_stretch(t, 1);
    CHECK(ids(t) == std::vector<save::model::ObjectId> { 1, 2, 3, 4, 6, 7 });
    REQUIRE(t.segments.size() == 2);
    CHECK(t.segments[1].piece_count == 4);
    CHECK(t.segments[0].closed);
}

TEST_CASE("only pieces that end where the next piece starts, and meet its start, fit between two")
{
    const save::model::TerrainGrid g = flat();
    // A plain straight, and one whose end is capped, which the plain straight after it does not meet.
    const std::vector<PieceShape> pieces { straight(), straight("cap") };
    const TrackTip tip = tip_of(placed(5, 5), pieces[0], false, g);
    const TrackTip goal = start_of(placed(7, 5), pieces[0], false, g);
    const std::vector<LaidPiece> fit = pieces_between(pieces, tip, { 0, false }, goal, { 0, false }, g);
    // The plain straight fits either way round.
    CHECK(fit == std::vector<LaidPiece> { { 0, false }, { 0, true } });
    // A gap two tiles long takes no one piece.
    CHECK(pieces_between(pieces, tip, { 0, false }, start_of(placed(8, 5), pieces[0], false, g), { 0, false }, g).empty());
}

TEST_CASE("a stretch made last is built on, and a piece added to another keeps the last where it was")
{
    save::model::Track t = stretched_track({ { 2, false }, { 3, false } });
    make_last(t, 0);
    CHECK(ids(t) == std::vector<save::model::ObjectId> { 3, 4, 5, 1, 2 });
    CHECK(add_piece(t, 0, { .id = 9 }) == 0);
    CHECK(ids(t) == std::vector<save::model::ObjectId> { 3, 4, 5, 9, 1, 2 });
    // With no stretch to add to, it begins one just before the last.
    CHECK(add_piece(t, std::nullopt, { .id = 8 }) == 1);
    CHECK(ids(t) == std::vector<save::model::ObjectId> { 3, 4, 5, 9, 8, 1, 2 });
    CHECK(t.segments.size() == 3);
    CHECK(t.segments[2].first_piece == 5);
}
