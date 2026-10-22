// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_completion.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <format>
#include <vector>

using namespace rct3;
using namespace rct3::park;

namespace
{
    constexpr float kTile = 4;

    save::TerrainGrid flat()
    {
        save::TerrainGrid g;
        g.width = 20;
        g.height = 20;
        g.tile_size_x = kTile;
        g.tile_size_z = kTile;
        g.cells.resize(400);
        return g;
    }

    PieceShape piece(std::string name, glm::vec3 end, glm::vec3 end_direction, bool chain = false)
    {
        PieceRecord r;
        r.name = name;
        r.item = std::format("{}Item", name);
        r.join_a = end_direction.x == 0 ? 3 : 0; // A tight curve pair, for the turning one
        r.flags = chain ? kPieceChain : 0;
        PieceLine l;
        l.start = { kTile / 2, 0, 0 };
        l.end = end;
        l.end_direction = end_direction;
        return shape_piece(r, l);
    }

    save::PlacedObject at(int col, int row)
    {
        save::PlacedObject o;
        o.col = col;
        o.row = row;
        o.force_absolute_height = true;
        return o;
    }

    bool anywhere(const save::PlacedObject&)
    {
        return true;
    }
} // namespace

TEST_CASE("a track is finished with the plain pieces that bring it round to its start")
{
    const save::TerrainGrid g = flat();
    // A straight, and a tight right turn on one tile.
    const std::vector pieces { piece("Straight", { -kTile / 2, 0, 0 }, { -1, 0, 0 }), piece("Turn", { 0, 0, kTile / 2 }, { 0, 0, 1 }) };
    const LaidPiece straight { 0, false };
    const save::PlacedObject first = at(5, 5);
    const TrackTip goal = start_of(first, pieces[0], false, g);
    const TrackTip tip = tip_of(first, pieces[0], false, g);
    const auto way = complete_track(pieces, tip, straight, goal, straight, g, anywhere, 1000);
    REQUIRE(way);
    // Round in a square, back to the tile before the first piece, and no further.
    REQUIRE(!way->empty());
    TrackTip end = tip;
    for (const LaidPlacement& p : *way)
        end = tip_of(p.object, pieces[p.piece.index], p.piece.reversed, g);
    CHECK(glm::length(end.position - goal.position) < 0.1f);
    CHECK(end.end == goal.end);
    CHECK(std::ranges::count_if(*way, [](const LaidPlacement& p) { return p.piece.index == 1; }) == 4);
}

TEST_CASE("a track that cannot be brought round says so, and gives up after its tries")
{
    const save::TerrainGrid g = flat();
    const std::vector pieces { piece("Straight", { -kTile / 2, 0, 0 }, { -1, 0, 0 }), piece("Lift", { -kTile / 2, 0, 0 }, { -1, 0, 0 }, true) };
    const LaidPiece straight { 0, false };
    const save::PlacedObject first = at(5, 5);
    const TrackTip goal = start_of(first, pieces[0], false, g);
    const TrackTip tip = tip_of(first, pieces[0], false, g);
    // Straights only run away from the start; a chain is never laid.
    CHECK(complete_track(pieces, tip, straight, goal, straight, g, anywhere, 10).error() == CompletionFailure::OutOfTries);
    const std::span lift_only = std::span(pieces).subspan(1);
    CHECK(complete_track(lift_only, tip, straight, goal, straight, g, anywhere, 10).error() == CompletionFailure::NoWay);
}

TEST_CASE("a track is finished only with pieces allowed where they stand")
{
    const save::TerrainGrid g = flat();
    const std::vector pieces { piece("Straight", { -kTile / 2, 0, 0 }, { -1, 0, 0 }), piece("Turn", { 0, 0, kTile / 2 }, { 0, 0, 1 }) };
    const LaidPiece straight { 0, false };
    const save::PlacedObject first = at(5, 5);
    const TrackTip goal = start_of(first, pieces[0], false, g);
    const TrackTip tip = tip_of(first, pieces[0], false, g);
    // Kept to the columns 3..6: the way round must turn back inside them.
    const auto inside = [](const save::PlacedObject& o) { return o.col >= 3 && o.col <= 6; };
    const auto way = complete_track(pieces, tip, straight, goal, straight, g, inside, 1000);
    REQUIRE(way);
    CHECK(std::ranges::all_of(*way, [&](const LaidPlacement& p) { return inside(p.object); }));
    CHECK(complete_track(pieces, tip, straight, goal, straight, g, [](const save::PlacedObject&) { return false; }, 1000).error() == CompletionFailure::NoWay);
}
