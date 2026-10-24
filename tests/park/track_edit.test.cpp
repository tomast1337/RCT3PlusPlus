// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_edit.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3;
using namespace rct3::park;

namespace
{
    constexpr float kTile = 4;

    /// A track of pieces with ids 1 to `count`, in stretches of the sizes `stretches` gives, closed as they say.
    save::Track track(std::vector<std::pair<std::size_t, bool>> stretches)
    {
        save::Track t;
        for (const auto& [count, closed] : stretches)
        {
            t.segments.push_back({ .first_piece = t.pieces.size(), .piece_count = count, .closed = closed });
            for (std::size_t i = 0; i < count; ++i)
                t.pieces.push_back({ .id = t.pieces.size() + 1 });
        }
        return t;
    }

    std::vector<save::ObjectId> ids(const save::Track& t)
    {
        std::vector<save::ObjectId> out;
        for (const save::TrackPiece& p : t.pieces)
            out.push_back(p.id);
        return out;
    }

    save::TerrainGrid flat()
    {
        save::TerrainGrid g;
        g.width = 10;
        g.height = 10;
        g.tile_size_x = kTile;
        g.tile_size_z = kTile;
        g.cells.resize(100);
        return g;
    }

    save::PlacedObject placed(int col, int row)
    {
        save::PlacedObject o;
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
    const save::Track t = track({ { 4, false }, { 3, true } });
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
    save::Track whole = track({ { 3, false } });
    whole.segments.clear();
    whole.circuit = true;
    CHECK(piece_neighbours(whole, 0));
}

TEST_CASE("a piece taken out of a closed stretch opens it there, building on from the piece before")
{
    save::Track t = track({ { 5, true } });
    t.circuit = true;
    CHECK(take_out_piece(t, 1).id == 2);
    CHECK(ids(t) == std::vector<save::ObjectId> { 3, 4, 5, 1 });
    REQUIRE(t.segments.size() == 1);
    CHECK_FALSE(t.segments[0].closed);
    CHECK(t.segments[0].piece_count == 4);
    CHECK_FALSE(t.circuit);
}

TEST_CASE("a piece taken out of an open stretch splits it, the pieces before it last, and the halves join again")
{
    save::Track t = track({ { 2, true }, { 5, false } });
    CHECK(take_out_piece(t, 4).id == 5);
    CHECK(ids(t) == std::vector<save::ObjectId> { 1, 2, 6, 7, 3, 4 });
    REQUIRE(t.segments.size() == 3);
    CHECK(t.segments[1].first_piece == 2);
    CHECK(t.segments[2].first_piece == 4);
    CHECK(t.segments[2].piece_count == 2);
    CHECK(t.pieces[5].segment == 2);
    // Built across the gap, the stretch after it joins on.
    join_stretch(t, 1);
    CHECK(ids(t) == std::vector<save::ObjectId> { 1, 2, 3, 4, 6, 7 });
    REQUIRE(t.segments.size() == 2);
    CHECK(t.segments[1].piece_count == 4);
    CHECK(t.segments[0].closed);
}

TEST_CASE("only pieces that end where the next piece starts, and meet its start, fit between two")
{
    const save::TerrainGrid g = flat();
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
