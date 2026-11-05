// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/pools.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{

    /// An invented price list, shaped like a pool type's.
    constexpr std::array<std::int32_t, 18> kPrices { 0, 0, 100, 300, 50, -200, 70, 10, -60, -5, 11, -9, 900, -800, 25, -15, 40, -30 };

    /// A pool of `cols` by `rows` water squares, ringed by paved squares, its corner at (0, 0).
    PoolMap ringed(std::int32_t cols, std::int32_t rows)
    {
        PoolMap map;
        for (std::int32_t row = 0; row < rows + 2; ++row)
            for (std::int32_t col = 0; col < cols + 2; ++col)
            {
                const bool edge = col == 0 || row == 0 || col == cols + 1 || row == rows + 1;
                map.squares.push_back({ .col = col, .row = row, .kind = edge ? PoolKind::Tile : PoolKind::Water, .surface = 4, .depth = 2 });
            }
        return map;
    }

    std::vector<PoolWallPiece> walls_at(const PoolMap& map, std::int32_t col, std::int32_t row)
    {
        return pool_walls(water_around(map, *map.at(col, row)));
    }

    bool has(const std::vector<PoolWallPiece>& walls, PoolWall shape, std::int32_t turn)
    {
        return std::ranges::any_of(walls, [&](const PoolWallPiece& w) { return w.shape == shape && w.turn == turn; });
    }

} // namespace

TEST_CASE("pools: a ringed pool has edges along its sides and outer corners in its corners")
{
    const PoolMap map = ringed(6, 6);
    CHECK(walls_at(map, 3, 3).empty());
    // Along the row before, the column before, the next row and the next column.
    CHECK(walls_at(map, 3, 1).size() == 1);
    CHECK(has(walls_at(map, 3, 1), PoolWall::Edge, 2));
    CHECK(has(walls_at(map, 1, 3), PoolWall::Edge, 3));
    CHECK(has(walls_at(map, 3, 6), PoolWall::Edge, 0));
    CHECK(has(walls_at(map, 6, 3), PoolWall::Edge, 1));
    CHECK(walls_at(map, 1, 1).size() == 1);
    CHECK(has(walls_at(map, 1, 1), PoolWall::OuterCorner, 2));
    CHECK(has(walls_at(map, 6, 1), PoolWall::OuterCorner, 1));
    CHECK(has(walls_at(map, 1, 6), PoolWall::OuterCorner, 3));
    CHECK(has(walls_at(map, 6, 6), PoolWall::OuterCorner, 0));
}

TEST_CASE("pools: a corner missing its water takes an inner corner")
{
    // An L: the square at the next column and next row of (1, 1) is paved.
    PoolMap map = ringed(3, 3);
    std::ranges::find_if(map.squares, [](const PoolSquare& s) { return s.col == 2 && s.row == 2; })->kind = PoolKind::Tile;
    const auto walls = walls_at(map, 1, 1);
    CHECK(has(walls, PoolWall::OuterCorner, 2));
    CHECK(has(walls, PoolWall::InnerCorner, 0));
    CHECK(walls.size() == 2);
}

TEST_CASE("pools: a lone square, an end and a channel")
{
    CHECK(pool_walls(0).size() == 1);
    CHECK(has(pool_walls(0), PoolWall::Full, 0));
    // Water only at the next row: walls on the other three sides.
    CHECK(pool_walls(kNextRow).size() == 1);
    CHECK(has(pool_walls(kNextRow), PoolWall::End, 1));
    // Water at the next row and the row before: an edge on each side.
    const auto channel = pool_walls(kNextRow | kRowBefore);
    CHECK(channel.size() == 2);
    CHECK(has(channel, PoolWall::Edge, 1));
    CHECK(has(channel, PoolWall::Edge, 3));
}

TEST_CASE("pools: every way water lies round a square walls each dry side once and each dry corner between water")
{
    for (std::uint32_t mask = 0; mask < 256; ++mask)
    {
        CAPTURE(mask);
        const auto water = static_cast<std::uint8_t>(mask);
        const auto wet = [&](std::int32_t turn) { return (water & (1u << (turn & 3))) != 0; };
        std::array<int, 4> sides { };
        std::array<int, 4> corners { };
        for (const PoolWallPiece& w : pool_walls(water))
            switch (w.shape)
            {
                case PoolWall::Edge:
                    ++sides[static_cast<std::size_t>(w.turn)];
                    break;
                case PoolWall::OuterCorner:
                    ++sides[static_cast<std::size_t>(w.turn)];
                    ++sides[static_cast<std::size_t>((w.turn + 1) & 3)];
                    break;
                case PoolWall::InnerCorner:
                    ++corners[static_cast<std::size_t>(w.turn)];
                    break;
                case PoolWall::End: // Open a turn before its own
                    for (std::int32_t turn = 0; turn < 4; ++turn)
                        sides[static_cast<std::size_t>(turn)] += turn != ((w.turn + 3) & 3) ? 1 : 0;
                    break;
                case PoolWall::Full:
                    for (int& side : sides)
                        ++side;
                    break;
            }
        for (std::int32_t turn = 0; turn < 4; ++turn)
        {
            CHECK(sides[static_cast<std::size_t>(turn)] == (wet(turn) ? 0 : 1));
            CHECK(corners[static_cast<std::size_t>(turn)] == (wet(turn) && wet(turn + 1) && (water & (kNextBoth << turn)) == 0 ? 1 : 0));
        }
        // Laid straight, each wall is a stack of its own pieces turned as the wall.
        PoolMap map { { { .col = 1, .row = 1, .surface = 4, .depth = 2 } } };
        constexpr std::array<std::array<std::int32_t, 2>, 8> kAround { { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 }, { 1, 1 }, { 1, -1 }, { -1, -1 }, { -1, 1 } } };
        for (std::size_t bit = 0; bit < kAround.size(); ++bit)
            if (water & (1u << bit))
                map.squares.push_back({ .col = 1 + kAround[bit][0], .row = 1 + kAround[bit][1], .surface = 4, .depth = 2 });
        CHECK(water_around(map, *map.at(1, 1)) == water);
        std::vector<std::pair<std::string_view, std::int32_t>> want;
        constexpr std::array<std::string_view, 5> kTop { "EdgeTop", "Corner1Top", "CurveCorner1Top", "EndTop", "FullTop" }; // By PoolWall
        for (const PoolWallPiece& w : pool_walls(water))
            want.emplace_back(kTop[static_cast<std::size_t>(w.shape)], w.turn);
        std::vector<std::pair<std::string_view, std::int32_t>> got;
        for (const PoolPiece& p : pool_pieces(map, *map.at(1, 1)))
            if (p.symbol.ends_with("Top"))
                got.emplace_back(p.symbol, p.turn);
        CHECK(got == want);
    }
}

namespace
{

    /// `map` with its water squares laid with `edge`.
    PoolMap edged(PoolMap map, PoolEdge edge)
    {
        for (PoolSquare& s : map.squares)
            s.edge = edge;
        return map;
    }

    /// The top pieces of the walls of the square at (`col`, `row`), as name and turn.
    std::vector<std::pair<std::string_view, std::int32_t>> tops(const PoolMap& map, std::int32_t col, std::int32_t row)
    {
        std::vector<std::pair<std::string_view, std::int32_t>> out;
        for (const PoolPiece& p : pool_pieces(map, *map.at(col, row)))
            if (p.symbol.ends_with("Top") && !p.symbol.starts_with("Solid"))
                out.emplace_back(p.symbol, p.turn);
        return out;
    }

    using Tops = std::vector<std::pair<std::string_view, std::int32_t>>;

} // namespace

TEST_CASE("pools: the edge setting shapes a pool's outside corners")
{
    CHECK(tops(ringed(4, 4), 1, 1) == Tops { { "Corner1Top", 2 } });
    CHECK(tops(edged(ringed(4, 4), PoolEdge::Diagonal), 1, 1) == Tops { { "Corner2Top", 2 } });
    CHECK(tops(edged(ringed(4, 4), PoolEdge::Tight), 4, 4) == Tops { { "CurveCorner5Top", 0 } });
    // The edges beside a tight curve stay straight.
    CHECK(tops(edged(ringed(4, 4), PoolEdge::Tight), 2, 1) == Tops { { "EdgeTop", 2 } });
}

TEST_CASE("pools: a loose curve sweeps into the squares beside its corner")
{
    const PoolMap map = edged(ringed(5, 4), PoolEdge::Loose);
    CHECK(tops(map, 1, 1) == Tops { { "CurveCorner9Top", 2 } });
    CHECK(tops(map, 5, 4) == Tops { { "CurveCorner9Top", 0 } });
    // Along the row before: meeting the sweep at the column before, then at the next column.
    CHECK(tops(map, 2, 1) == Tops { { "CurveEdge1Top", 2 } });
    CHECK(tops(map, 3, 1) == Tops { { "EdgeTop", 2 } });
    CHECK(tops(map, 4, 1) == Tops { { "CurveEdge2Top", 2 } });
    // Down the column before: meeting the sweep at the row before.
    CHECK(tops(map, 1, 2) == Tops { { "CurveEdge2Top", 3 } });
    // Three wide, the middle square meets a sweep at both ends.
    CHECK(tops(edged(ringed(3, 3), PoolEdge::Loose), 2, 1) == Tops { { "CurveEdge3Top", 2 } });
}

TEST_CASE("pools: a loose curve beside a straight wall is tight, and the curves beside it meet its neighbours' sweeps")
{
    PoolMap map = edged(ringed(4, 4), PoolEdge::Loose);
    std::ranges::find_if(map.squares, [](const PoolSquare& s) { return s.col == 2 && s.row == 1; })->edge = PoolEdge::Straight;
    CHECK(tops(map, 1, 1) == Tops { { "CurveCorner5Top", 2 } });
    CHECK(tops(map, 1, 2) == Tops { { "EdgeTop", 3 } });
    // Two wide: each corner tight beside a sweep along the next column or the row before.
    PoolMap narrow = edged(ringed(2, 4), PoolEdge::Loose);
    std::ranges::find_if(narrow.squares, [](const PoolSquare& s) { return s.col == 1 && s.row == 1; })->edge = PoolEdge::Tight;
    CHECK(tops(narrow, 1, 1) == Tops { { "CurveCorner7Top", 2 } });
}

TEST_CASE("pools: an inside corner has its own wall, a plain one where a corner cut across meets it")
{
    // An L of water: rows 1 and 2 four long, then two more rows two wide; the inside corner at (2, 2).
    PoolMap map;
    for (std::int32_t row = 1; row <= 4; ++row)
        for (std::int32_t col = 1; col <= (row <= 2 ? 4 : 2); ++col)
            map.squares.push_back({ .col = col, .row = row, .kind = PoolKind::Water, .surface = 4, .depth = 2 });
    // Turned to the first of the two sides round it: the next row.
    CHECK(tops(map, 2, 2) == Tops { { "CurveCorner1Top", 0 } });
    // A ring round a paved middle: each corner square's square outside corner leaves its inside corner as it is.
    PoolMap ring = ringed(3, 3);
    std::ranges::find_if(ring.squares, [](const PoolSquare& s) { return s.col == 2 && s.row == 2; })->kind = PoolKind::Tile;
    CHECK(std::ranges::contains(tops(ring, 1, 1), std::pair { std::string_view("CurveCorner1Top"), 0 }));
    // Laid cut across, the corner squares' inside corners are plain.
    CHECK(tops(edged(ring, PoolEdge::Diagonal), 1, 1) == Tops { { "Corner2Top", 2 }, { "Corner3Top", 0 } });
}

TEST_CASE("pools: a loose corner with water round its inside too does not sweep, nor do the squares beside it meet it")
{
    // Water at the row before and the column before of (1, 1), not at the corner between: an outside corner and an
    // inside one in the same square.
    PoolMap map;
    for (const auto [col, row] : { std::pair { 0, 1 }, { 1, 0 }, { 1, 1 } })
        map.squares.push_back({ .col = col, .row = row, .kind = PoolKind::Water, .surface = 4, .depth = 2, .edge = PoolEdge::Loose });
    CHECK(tops(map, 1, 1) == Tops { { "CurveCorner5Top", 0 }, { "CurveCorner1Top", 2 } });
}

TEST_CASE("pools: an inside corner meeting a sweep at its second side, or at both, is turned a turn on")
{
    // Loose water three by two, then two more squares under its first two: the inside corner at (2, 2), between the
    // next row and the next column, both of whose squares sweep round their own corners.
    const auto loose = [](std::int32_t rows) {
        PoolMap map;
        for (std::int32_t row = 1; row <= rows; ++row)
            for (std::int32_t col = 1; col <= (row <= 2 ? 3 : 2); ++col)
                map.squares.push_back({ .col = col, .row = row, .kind = PoolKind::Water, .surface = 4, .depth = 2, .edge = PoolEdge::Loose });
        return map;
    };
    CHECK(tops(loose(3), 2, 3) == Tops { { "CurveCorner9Top", 0 } });
    CHECK(tops(loose(3), 2, 2) == Tops { { "CurveCorner4Top", 1 } });
    // A row longer under it, the next row's square is an edge: only the next column's square sweeps.
    CHECK(tops(loose(4), 2, 2) == Tops { { "CurveCorner2Top", 1 } });
}

TEST_CASE("pools: laid curved, the ends of strips and lone squares are rounded")
{
    PoolMap strip { { { .col = 0, .row = 0, .kind = PoolKind::Water, .surface = 4, .depth = 2, .edge = PoolEdge::Tight }, { .col = 0, .row = 1, .kind = PoolKind::Water, .surface = 4, .depth = 2 } } };
    CHECK(tops(strip, 0, 0) == Tops { { "CurveEndTop", 1 } });
    CHECK(tops(strip, 0, 1) == Tops { { "EndTop", 3 } });
}

TEST_CASE("pools: a square is a stack from its floor to its surface, railed where the pool ends")
{
    const PoolMap map = ringed(2, 2);
    const auto tile = pool_pieces(map, *map.at(0, 0));
    REQUIRE(tile.size() == 5);
    CHECK(tile[0].symbol == "SolidBot");
    CHECK(tile[0].height == 2);
    CHECK(tile[1].symbol == "SolidMid");
    CHECK(tile[2].symbol == "SolidTop");
    CHECK(tile[2].height == 4);
    CHECK(tile[3].symbol == "Railing");
    CHECK(tile[3].turn == 2);
    CHECK(tile[4].turn == 3);
    PoolMap shallow { { { .col = 0, .row = 0, .kind = PoolKind::Water, .surface = 4, .depth = 1 }, { .col = 0, .row = 1, .kind = PoolKind::Water, .surface = 4, .depth = 1 } } };
    const auto water = pool_pieces(shallow, shallow.squares[0]);
    // A floor, an end of two pieces, three railings.
    REQUIRE(water.size() == 6);
    CHECK(water[0].symbol == "Base");
    CHECK(water[0].height == 3);
    CHECK(water[1].symbol == "EndBot");
    CHECK(water[2].symbol == "EndTop");
    CHECK(water[2].height == 4);
    CHECK(std::ranges::count(water, std::string_view("Railing"), &PoolPiece::symbol) == 3);
    CHECK(pool_objects(shallow, "Pools/Own", 10).front().id == 11);
}

TEST_CASE("pools: a spa pool tops its paved stack")
{
    const PoolMap map { { { .col = 0, .row = 0, .kind = PoolKind::Jacuzzi, .surface = 0, .depth = 2 } } };
    const auto pieces = pool_pieces(map, map.squares[0]);
    CHECK(pieces[2].symbol == "Jacuzzi");
}

TEST_CASE("pools: a high diving board climbs from its paved stack in levels, a board at each")
{
    const PoolMap map;
    std::vector<std::pair<std::string_view, std::int32_t>> above;
    for (const PoolPiece& p : pool_pieces(map, { .kind = PoolKind::HighDive, .surface = 4, .depth = 2, .board = 10, .turn = 1 }))
        if (p.height > 4 || p.symbol.starts_with("DB"))
        {
            above.emplace_back(p.symbol, p.height);
            CHECK(p.turn == 3);
        }
    using Piece = std::pair<std::string_view, std::int32_t>;
    CHECK(above == std::vector<Piece> { { "DBBase", 4 }, { "DBMidBoard", 6 }, { "DBMid", 7 }, { "DBMidBoard", 9 }, { "DBMid", 10 }, { "DBTopBoard", 12 } });
    // Its climb is paid by the unit, in whole levels.
    CHECK(pool_build_cost(kPrices, { .kind = PoolKind::HighDive, .depth = 2, .board = 10 }) == 70 + 2 * 10 + 9 * 40);
    CHECK(pool_refund(kPrices, { .kind = PoolKind::HighDive, .depth = 2, .board = 10 }) == -60 - 2 * 5 - 9 * 30);
}

TEST_CASE("pools: a high diving board's boards are named by the first of the three squares they cover")
{
    // Water at the next column: the boards lie along the columns, the tower's square in their middle.
    const PoolMap map { { { .col = 5, .row = 7, .kind = PoolKind::HighDive, .surface = 0, .depth = 2, .board = 6, .turn = 1 } } };
    for (const save::PlacedObject& o : pool_objects(map, "Pools/Own", 0))
    {
        const bool board = o.symbol == "DBMidBoard" || o.symbol == "DBTopBoard";
        CHECK(o.col == (board ? 4 : 5));
        CHECK(o.row == 7);
    }
}

TEST_CASE("pools: a high diving board laid on a water square paves it, walls the water beside it and refunds the water")
{
    PoolMap map = ringed(3, 3);
    const PoolSquare water = *map.at(3, 2);
    // At the wall on the next column, facing the next row's water.
    const PoolSquare dive { .col = 3, .row = 2, .kind = PoolKind::HighDive, .surface = 4, .depth = 2, .board = 3, .turn = 0 };
    CHECK(lay_pool(map, kPrices, std::array { dive }) == pool_build_cost(kPrices, dive) + pool_refund(kPrices, water));
    const auto pieces = pool_pieces(map, *map.at(3, 2));
    CHECK(std::ranges::contains(pieces, std::string_view("SolidTop"), &PoolPiece::symbol));
    CHECK(std::ranges::contains(pieces, std::string_view("DBBase"), &PoolPiece::symbol));
    CHECK(!std::ranges::contains(pieces, std::string_view("Base"), &PoolPiece::symbol));
    // The water in front, in the pool's corner, is walled at the tower too.
    CHECK(std::ranges::any_of(walls_at(map, 3, 3), [](const PoolWallPiece& w) { return w.shape == PoolWall::End; }));
    CHECK(pool_water(map).size() == 8);
}

TEST_CASE("pools: a saved high diving board faces the side it keeps, down past water at its own level")
{
    save::WorldModel world;
    // A raised pool a row back at the tower's level, a low pool in front of it.
    world.pool_segments = {
        { .id = 1, .col = 5, .row = 4, .type = 0, .surface = 16, .depth = 2 },
        { .id = 2, .col = 5, .row = 5, .type = 3, .surface = 16, .special = 19, .side = 2 },
        { .id = 3, .col = 5, .row = 6, .type = 0, .surface = 3, .depth = 2 },
    };
    CHECK(saved_pools(world, { }).at(5, 5)->turn == 0);
    // Kept without a side, it faces the first water beside it.
    world.pool_segments[1].side = 0;
    world.pool_segments[0].type = 1;
    CHECK(saved_pools(world, { }).at(5, 5)->turn == 0);
}

TEST_CASE("pools: a spiral staircase winds up from its paved stack, railed at its top where no square meets it")
{
    // Its foot at the next column, a paved square at the top level at the row before.
    PoolMap map { { { .col = 0, .row = 0, .kind = PoolKind::Stairs, .surface = 4, .depth = 2, .board = 5, .turn = 0 }, { .col = 0, .row = -1, .kind = PoolKind::Tile, .surface = 9, .depth = 2 } } };
    std::vector<std::pair<std::string_view, std::int32_t>> steps;
    std::vector<std::int32_t> turns;
    std::vector<std::int32_t> top_rails;
    for (const PoolPiece& p : pool_pieces(map, map.squares[0]))
    {
        if (p.symbol.starts_with("Stairs") || p.symbol == "SolidTop")
        {
            steps.emplace_back(p.symbol, p.height);
            turns.push_back(p.turn);
        }
        if (p.symbol == "Railing" && p.height == 9)
            top_rails.push_back(p.turn);
    }
    using Piece = std::pair<std::string_view, std::int32_t>;
    CHECK(steps == std::vector<Piece> { { "StairsBase", 4 }, { "StairsMid", 5 }, { "StairsMid", 6 }, { "StairsMid", 7 }, { "StairsTop", 8 } });
    CHECK(turns == std::vector<std::int32_t> { 0, 3, 2, 1, 0 });
    CHECK(top_rails == std::vector<std::int32_t> { 0, 1, 3 });
    // Its climb is paid by the unit.
    CHECK(pool_build_cost(kPrices, { .kind = PoolKind::Stairs, .depth = 2, .board = 5 }) == 70 + 2 * 10 + 5 * 25);
    CHECK(pool_refund(kPrices, { .kind = PoolKind::Stairs, .depth = 2, .board = 5 }) == -60 - 2 * 5 - 5 * 15);
}

TEST_CASE("pools: a pool path's piece follows the paths joining it, slopes by their foot and top")
{
    const auto piece = [](const PoolMap& map, std::int32_t col, std::int32_t row) {
        const auto pieces = pool_pieces(map, *map.at(col, row));
        REQUIRE(pieces.size() == 1);
        return std::pair { pieces[0].symbol, pieces[0].turn };
    };
    using Piece = std::pair<std::string_view, std::int32_t>;
    // A path along the columns, turning down the rows to a slope rising a unit away from it, then the path on.
    const PoolMap map { {
        { .col = 0, .row = 0, .kind = PoolKind::Path, .surface = 4, .depth = 0 },
        { .col = 1, .row = 0, .kind = PoolKind::Path, .surface = 4, .depth = 0 },
        { .col = 1, .row = 1, .kind = PoolKind::PathSlope, .surface = 4, .depth = 0, .turn = 2 },
        { .col = 1, .row = 2, .kind = PoolKind::Path, .surface = 5, .depth = 0 },
    } };
    CHECK(piece(map, 0, 0) == Piece { "PathStraight", 3 });
    CHECK(piece(map, 1, 0) == Piece { "PathCorner", 3 });
    CHECK(piece(map, 1, 1) == Piece { "PathSlope", 2 });
    CHECK(piece(map, 1, 2) == Piece { "PathStraight", 2 });
    // Four ways, and three.
    PoolMap cross;
    for (const auto [col, row] : { std::pair { 1, 1 }, { 0, 1 }, { 2, 1 }, { 1, 0 }, { 1, 2 } })
        cross.squares.push_back({ .col = col, .row = row, .kind = PoolKind::Path, .surface = 4, .depth = 0 });
    CHECK(piece(cross, 1, 1).first == "PathXRoads");
    cross.squares.pop_back();
    CHECK(piece(cross, 1, 1) == Piece { "PathTJunction", 1 });
    // A path costs its flat price.
    CHECK(pool_build_cost(kPrices, { .kind = PoolKind::Path, .depth = 0 }) == 11);
    CHECK(pool_refund(kPrices, { .kind = PoolKind::PathStairs, .depth = 0 }) == -9);
}

TEST_CASE("pools: a trail lays the next pool path piece from where its last one ended, its slope's foot at the low end")
{
    FootpathBuilder trail;
    CHECK_FALSE(next_pool_path(trail));
    start_at(trail, { 1, 0 });
    trail.start_height = 4;
    trail.trail.push_back({ .spot = { 1, 0 }, .laid = 1, .height = 4 });
    // Flat on along the rows, then up a slope: its foot towards the trail, its top a unit higher.
    auto next = next_pool_path(trail);
    REQUIRE(next);
    CHECK(next->square.col == 1);
    CHECK(next->square.row == 1);
    CHECK(next->square.kind == PoolKind::Path);
    CHECK(next->end == 4);
    trail.slope = PathSlope::Up;
    next = next_pool_path(trail);
    CHECK(next->square.kind == PoolKind::PathSlope);
    CHECK(next->square.surface == 4);
    CHECK(next->square.turn == 2);
    CHECK(next->end == 5);
    // Down steps from a height of 5: their foot ahead, two units lower.
    trail.trail.back().height = 5;
    trail.slope = PathSlope::SteepDown;
    next = next_pool_path(trail);
    CHECK(next->square.kind == PoolKind::PathStairs);
    CHECK(next->square.surface == 3);
    CHECK(next->square.turn == 0);
    CHECK(next->end == 3);
}

TEST_CASE("pools: a pool path trail starts at any side of the complex that no square meets, a slope only at its ends")
{
    PoolMap map = ringed(2, 1);
    // A paved corner starts from its two outer sides; the sides it shares with the ring and the water do not.
    const PoolSquare& corner = *map.at(0, 0);
    CHECK(pool_path_start(map, corner, 2) == 4);
    CHECK(pool_path_start(map, corner, 3) == 4);
    CHECK_FALSE(pool_path_start(map, corner, 0));
    CHECK_FALSE(pool_path_start(map, corner, 1));
    // Water inside the ring meets squares all round.
    for (std::int32_t turn = 0; turn < 4; ++turn)
        CHECK_FALSE(pool_path_start(map, *map.at(1, 1), turn));
    // A slope off the ring's top side, its foot on the paving: its top starts a trail a unit up, its sides none,
    // and the paving under its foot no longer starts one there.
    map.squares.push_back({ .col = 1, .row = -1, .kind = PoolKind::PathSlope, .surface = 4, .depth = 0, .turn = 0 });
    const PoolSquare& slope = map.squares.back();
    CHECK(pool_path_start(map, slope, 2) == 5);
    CHECK_FALSE(pool_path_start(map, slope, 1));
    CHECK_FALSE(pool_path_start(map, slope, 0));
    CHECK_FALSE(pool_path_start(map, *map.at(1, 0), 2));
    CHECK(pool_path_start(map, *map.at(2, 0), 2) == 4);
    // A square beside at another height does not meet the side.
    map.squares.push_back({ .col = -1, .row = 0, .kind = PoolKind::Tile, .surface = 9, .depth = 2 });
    CHECK(pool_path_start(map, *map.at(0, 0), 3) == 4);
}

TEST_CASE("pools: each square costs its kind's price and its depth's")
{
    CHECK(pool_build_cost(kPrices, { .kind = PoolKind::Water, .depth = 2 }) == 100 + 2 * 300);
    CHECK(pool_refund(kPrices, { .kind = PoolKind::Water, .depth = 2 }) == 50 - 2 * 200);
    CHECK(pool_build_cost(kPrices, { .kind = PoolKind::Tile, .depth = 3 }) == 70 + 3 * 10);
    CHECK(pool_build_cost(kPrices, { .kind = PoolKind::Jacuzzi, .depth = 2 }) == 70 + 2 * 10 + 900);
    CHECK(pool_refund(kPrices, { .kind = PoolKind::Jacuzzi, .depth = 2 }) == -60 - 2 * 5 - 800);
}

TEST_CASE("pools: laying over a square costs the new one and refunds the old")
{
    PoolMap map;
    const PoolSquare water { .col = 0, .row = 0, .kind = PoolKind::Water, .depth = 2 };
    CHECK(lay_pool(map, kPrices, std::array { water }) == 700);
    CHECK(lay_pool(map, kPrices, std::array { water }) == 0);
    PoolSquare tile = water;
    tile.kind = PoolKind::Tile;
    CHECK(lay_pool(map, kPrices, std::array { tile }) == 90 + (50 - 400));
    REQUIRE(map.squares.size() == 1);
    CHECK(map.squares[0].kind == PoolKind::Tile);
    CHECK(take_up_pool(map, kPrices, { 0, 0 }, { 3, 3 }) == -70);
    CHECK(map.squares.empty());
}

TEST_CASE("pools: a square laid well above another on its tile stacks over it; one laid close takes its place")
{
    PoolMap map;
    const PoolSquare low { .col = 2, .row = 2, .kind = PoolKind::Water, .surface = 3, .depth = 2 };
    (void)lay_pool(map, kPrices, std::array { low });
    // Its floor kPoolHeadroom over the low square's surface: stacked, paid for alone.
    const PoolSquare high { .col = 2, .row = 2, .kind = PoolKind::Tile, .surface = 3 + kPoolHeadroom + 2, .depth = 2 };
    CHECK(lay_pool(map, kPrices, std::array { high }) == pool_build_cost(kPrices, high));
    REQUIRE(map.squares.size() == 2);
    CHECK(map.at(2, 2)->surface == high.surface); // Seen from above
    CHECK(map.at(2, 2, 0)->surface == low.surface);
    // A unit lower, it would be in the low square's way: it takes its place, the low one refunded.
    PoolSquare close = high;
    close.surface -= 1;
    close.col = 3;
    (void)lay_pool(map, kPrices, std::array { PoolSquare { .col = 3, .row = 2, .kind = PoolKind::Water, .surface = 3, .depth = 2 } });
    CHECK(lay_pool(map, kPrices, std::array { close }) == pool_build_cost(kPrices, close) + pool_refund(kPrices, low));
    CHECK(std::ranges::count_if(map.squares, [](const PoolSquare& s) { return s.col == 3; }) == 1);
    // Taken up from the level of one of them, the other stays.
    CHECK(take_up_pool(map, kPrices, { 2, 2 }, { 2, 2 }, high.surface) == pool_refund(kPrices, high));
    REQUIRE(map.at(2, 2));
    CHECK(map.at(2, 2)->surface == low.surface);
}

TEST_CASE("pools: stacked squares are walled and railed by their own level")
{
    PoolMap map = ringed(2, 2);
    // A paved square over the corner of the pool, high above it.
    map.squares.push_back({ .col = 1, .row = 1, .kind = PoolKind::Tile, .surface = 20, .depth = 2 });
    const auto low = std::ranges::find_if(map.squares, [](const PoolSquare& s) { return s.col == 1 && s.row == 1 && s.surface == 4; });
    REQUIRE(low != map.squares.end());
    CHECK(water_around(map, *low) == water_around(ringed(2, 2), *ringed(2, 2).at(1, 1)));
    const auto railings = [&](const PoolSquare& s) { return std::ranges::count(pool_pieces(map, s), std::string_view("Railing"), &PoolPiece::symbol); };
    CHECK(railings(*low) == 0);
    CHECK(railings(*map.at(1, 1)) == 4);
}

TEST_CASE("pools: the ground is dug down to a square's floor and kept, and left under a raised square")
{
    save::TerrainGrid grid { .width = 2, .height = 1, .tile_size_x = 4, .tile_size_z = 4 };
    grid.cells.resize(2);
    for (save::TerrainCell& c : grid.cells)
        c.corners = { 5, 5, 6, 5 };
    PoolMap map;
    map.squares = { { .col = 0, .row = 0, .kind = PoolKind::Water, .surface = 5, .depth = 2 }, { .col = 1, .row = 0, .kind = PoolKind::Water, .surface = 16, .depth = 2 } };
    dig_under_pools(map, grid);
    CHECK(grid.at(0, 0).corners == std::array { 3.0f, 3.0f, 3.0f, 3.0f });
    REQUIRE(map.squares[0].ground);
    CHECK(*map.squares[0].ground == std::array { 5.0f, 5.0f, 6.0f, 5.0f });
    // Raised: the ground stays where it was, and there is none to put back.
    CHECK(grid.at(1, 0).corners == std::array { 5.0f, 5.0f, 6.0f, 5.0f });
    CHECK_FALSE(map.squares[1].ground);
    // Again, nothing changes: the ground kept is the one from before.
    dig_under_pools(map, grid);
    CHECK(*map.squares[0].ground == std::array { 5.0f, 5.0f, 6.0f, 5.0f });
    // Laid over, a square keeps the ground the one before it dug.
    (void)lay_pool(map, kPrices, std::array { PoolSquare { .col = 0, .row = 0, .kind = PoolKind::Tile, .surface = 5, .depth = 2 } });
    CHECK(map.at(0, 0)->ground == std::array { 5.0f, 5.0f, 6.0f, 5.0f });
}

TEST_CASE("pools: a park from the game keeps squares stacked on a tile, and the ground those sunk into it dug")
{
    save::WorldModel world;
    world.pool_segments = {
        { .id = 1, .col = 5, .row = 5, .type = 1, .surface = 3, .depth = 2, .ground = std::array { 0.0f, 0.0f, 0.0f, 0.0f } },
        { .id = 2, .col = 5, .row = 5, .type = 1, .surface = 16, .depth = 2, .ground = std::array { 0.0f, 0.0f, 0.0f, 0.0f } },
        { .id = 3, .col = 6, .row = 5, .type = 0, .surface = 0, .depth = 2, .ground = std::array { 0.0f, 0.0f, 0.0f, 0.0f } },
    };
    const park::PoolMap map = park::saved_pools(world, { });
    REQUIRE(map.squares.size() == 3);
    CHECK(map.at(5, 5, 0)->surface == 3);
    CHECK(map.at(5, 5)->surface == 16);
    CHECK_FALSE(map.at(5, 5, 0)->ground); // Its floor over the ground
    CHECK(map.at(6, 5)->ground == std::array { 0.0f, 0.0f, 0.0f, 0.0f });
}

TEST_CASE("pools: an extra's words name what it takes of its square")
{
    const PoolExtraFit fit = pool_extra_fit(" side centralsolid WholeTile HeightStep 2 Shower");
    CHECK(fit.side);
    CHECK(fit.paved_middle);
    CHECK(fit.whole);
    CHECK(fit.step == 2);
    CHECK_FALSE(fit.wall);
    CHECK_FALSE(fit.water_middle);
    CHECK(fit.shower);
    CHECK_FALSE(fit.lounger);
    CHECK(pool_extra_fit("CentralSolid WholeTile SunLounger").lounger);
    CHECK(pool_extra_fit("WaveMachine UnderWaterWall").wave_machine);
    CHECK(pool_extra_fit("WaveMachine UnderWaterWall").wall);
    CHECK(pool_extra_fit("Side CentralWater OppositeWater DivingBoard").board);
    CHECK(pool_extra_fit("Side UnderWaterWall Ladder").ladder);
    CHECK_FALSE(fit.board);
}

TEST_CASE("pools: an extra fits where its square offers all it takes and no other extra has it")
{
    PoolMap map = ringed(3, 3);
    const PoolExtraFit middle { .paved_middle = true };
    const PoolExtraFit whole { .paved_middle = true, .whole = true };
    const PoolExtraFit on_wall { .side = true, .wall = true };
    const PoolExtraFit board { .water_middle = true, .side = true, .water_across = true };
    // Paved squares take middle extras; water squares do not.
    CHECK(pool_extra_fits(map, 0, 0, middle, 0));
    CHECK_FALSE(pool_extra_fits(map, 2, 2, middle, 0));
    // Wall extras go on a walled side of a water square only: (1, 2) is walled towards the column before.
    CHECK(pool_extra_fits(map, 1, 2, on_wall, 3));
    CHECK_FALSE(pool_extra_fits(map, 1, 2, on_wall, 1));
    CHECK_FALSE(pool_extra_fits(map, 2, 2, on_wall, 3));
    CHECK_FALSE(pool_extra_fits(map, 0, 2, on_wall, 3));
    // A board needs water across from its wall.
    CHECK(pool_extra_fits(map, 1, 2, board, 3));
    // What one extra takes, another may not.
    map.extras.push_back({ .col = 0, .row = 0, .turn = 0, .fit = middle });
    CHECK_FALSE(pool_extra_fits(map, 0, 0, middle, 1));
    CHECK_FALSE(pool_extra_fits(map, 0, 0, whole, 2));
    map.extras.push_back({ .col = 1, .row = 2, .turn = 3, .fit = on_wall });
    CHECK_FALSE(pool_extra_fits(map, 1, 2, on_wall, 3));
    CHECK_FALSE(pool_extra_fits(map, 1, 2, board, 3));
    CHECK_FALSE(pool_extra_fits(map, 9, 9, middle, 0));
}

TEST_CASE("pools: extras go with their square, refunded")
{
    PoolMap map = ringed(1, 1);
    map.extras.push_back({ .col = 0, .row = 0, .refund = -30, .fit = { .paved_middle = true } });
    map.extras.push_back({ .col = 2, .row = 2, .refund = -40, .fit = { .paved_middle = true } });
    CHECK(take_up_pool(map, kPrices, { 0, 0 }, { 0, 0 }) == -60 - 2 * 5 - 30);
    REQUIRE(map.extras.size() == 1);
    PoolSquare water { .col = 2, .row = 2, .kind = PoolKind::Water, .surface = 4, .depth = 2 };
    CHECK(lay_pool(map, kPrices, std::array { water }) == 700 + (-60 - 2 * 5) - 40);
    CHECK(map.extras.empty());
    map.extras.push_back({ .symbol = "Lamp", .overlay = "Pools/Own", .col = 1, .row = 1, .turn = 3 });
    const auto objects = pool_objects(map, "Pools/Own", 0);
    CHECK(objects.back().symbol == "Lamp");
    CHECK(objects.back().rotation == 3);
    CHECK(objects.back().base_height == 4);
}

TEST_CASE("pools: a changing room stands off the pool, opening on a paved square, and leaves its railing open")
{
    PoolMap map = ringed(2, 2);
    CHECK(pool_room_fits(map, -1, 1, 1));
    CHECK_FALSE(pool_room_fits(map, -1, 1, 3));
    CHECK_FALSE(pool_room_fits(map, 0, 1, 1));
    CHECK(pool_room_fits(map, 1, -1, 0));
    const auto railings = [&] { return std::ranges::count(pool_pieces(map, *map.at(0, 1)), std::string_view("Railing"), &PoolPiece::symbol); };
    CHECK(railings() == 1);
    map.rooms.push_back({ .col = -1, .row = 1, .turn = 1 });
    CHECK(railings() == 0);
    CHECK_FALSE(pool_room_fits(map, -1, 1, 1));
    const auto objects = pool_objects(map, "Pools/Own", 0);
    CHECK(objects.back().col == -1);
    CHECK(objects.back().rotation == 2);
    CHECK(objects.back().base_height == 4);
}

TEST_CASE("pools: a ramp opens the railings across it, on its own square and on the square it climbs to")
{
    // A paved square with a higher one at each side, two up at the next row, one up at the next column.
    const auto railed = [](const PoolMap& map, const PoolSquare& s) {
        std::vector<std::int32_t> turns;
        for (const PoolPiece& p : pool_pieces(map, s))
            if (p.symbol == "Railing")
                turns.push_back(p.turn);
        return turns;
    };
    for (std::int32_t turn = 0; turn < 4; ++turn)
    {
        CAPTURE(turn);
        constexpr std::array<std::array<std::int32_t, 2>, 4> kStep { { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } } };
        const auto [dc, dr] = kStep[static_cast<std::size_t>(turn)];
        PoolMap map { { { .col = 0, .row = 0, .kind = PoolKind::Tile, .surface = 4 }, { .col = dc, .row = dr, .kind = PoolKind::Tile, .surface = 6 } } };
        const auto all_but = [](std::int32_t open) {
            std::vector<std::int32_t> turns;
            for (std::int32_t t = 0; t < 4; ++t)
                if (t != open)
                    turns.push_back(t);
            return turns;
        };
        CHECK(railed(map, map.squares[0]).size() == 4);
        CHECK(railed(map, map.squares[1]).size() == 4);
        // It goes up to the higher square, or off the pool; not down, nor against a square as high.
        const PoolExtraFit up = pool_extra_fit("Side HeightStep 1");
        map.squares.push_back({ .col = -dc, .row = -dr, .kind = PoolKind::Tile, .surface = 4 });
        CHECK(pool_extra_fits(map, 0, 0, up, turn));
        CHECK(pool_extra_fits(map, 0, 0, up, turn + 1));
        CHECK_FALSE(pool_extra_fits(map, 0, 0, up, turn + 2));
        CHECK_FALSE(pool_extra_fits(map, dc, dr, up, turn + 2));
        map.squares.pop_back();
        // A ramp climbing one, short of the square beside, leaves both railed.
        map.extras.push_back({ .col = 0, .row = 0, .turn = turn, .fit = pool_extra_fit("Side HeightStep 1") });
        CHECK(railed(map, map.squares[0]).size() == 4);
        CHECK(railed(map, map.squares[1]).size() == 4);
        // One climbing two meets it: open at its foot's side and at the top's side towards it.
        map.extras.back().fit = pool_extra_fit("Side CentralSolid WholeTile HeightStep 2");
        CHECK(railed(map, map.squares[0]) == all_but(turn));
        CHECK(railed(map, map.squares[1]) == all_but((turn + 2) & 3));
        // Turned away from it, the ramp faces nothing and both are railed all round.
        map.extras.back().turn = turn + 1;
        CHECK(railed(map, map.squares[0]).size() == 4);
        CHECK(railed(map, map.squares[1]).size() == 4);
    }
}

TEST_CASE("pools: a square at a spiral staircase's top is open towards it, one at its foot level too")
{
    PoolMap map { { { .col = 0, .row = 0, .kind = PoolKind::Stairs, .surface = 4, .depth = 2, .board = 5, .turn = 1 }, { .col = 0, .row = -1, .kind = PoolKind::Tile, .surface = 9, .depth = 2 }, { .col = 1, .row = 0, .kind = PoolKind::Tile, .surface = 4, .depth = 2 }, { .col = -1, .row = 0, .kind = PoolKind::Tile, .surface = 7, .depth = 2 } } };
    const auto railed = [&](std::size_t square, std::int32_t turn) { return std::ranges::any_of(pool_pieces(map, map.squares[square]), [&](const PoolPiece& p) { return p.symbol == "Railing" && p.turn == turn; }); };
    CHECK_FALSE(railed(1, 0)); // At its top
    CHECK_FALSE(railed(2, 3)); // At its foot
    CHECK(railed(3, 1));       // Neither: between its foot and its top
}

TEST_CASE("pools: a changing room fits on every side of a pool, turned towards it")
{
    const PoolMap map = ringed(2, 2);
    // West, south, east and north of the paved ring, each opening on the square beside it alone.
    for (const auto [col, row, turn] : { std::array { -1, 1, 1 }, std::array { 1, -1, 0 }, std::array { 4, 2, 3 }, std::array { 2, 4, 2 } })
        for (std::int32_t other = 0; other < 4; ++other)
            CHECK(pool_room_fits(map, col, row, other) == (other == turn));
}

TEST_CASE("pools: a complex is the squares that meet side to side, and the changing rooms opening on them")
{
    PoolMap map = ringed(2, 2);
    // Another pool, apart from the first.
    map.squares.push_back({ .col = 10, .row = 0, .kind = PoolKind::Tile, .surface = 4, .depth = 2 });
    map.rooms.push_back({ .col = -1, .row = 1, .turn = 1 });
    map.rooms.push_back({ .col = 9, .row = 0, .turn = 1 });
    map.rooms.push_back({ .col = 1, .row = 4, .turn = 2 });
    CHECK(pool_complex_rooms(map, 0) == std::vector<std::size_t> { 0, 2 });
    CHECK(pool_complex_rooms(map, 1) == std::vector<std::size_t> { 1 });
    CHECK(pool_complex(map, 1).size() == 1);
    // A room goes with the square it opens on.
    (void)take_up_pool(map, kPrices, { 10, 0 }, { 10, 0 });
    CHECK(map.rooms.size() == 2);
    (void)lay_pool(map, kPrices, std::array { PoolSquare { .col = 0, .row = 1, .kind = PoolKind::Water, .surface = 4, .depth = 2 } });
    CHECK(map.rooms.size() == 1);
}

TEST_CASE("pools: a park from the game gives its squares, boards and changing rooms to the pool tool")
{
    save::WorldModel world;
    world.pool_segments = {
        { .id = 1, .col = 5, .row = 5, .type = 0, .surface = 4, .depth = 2 }, { .id = 2, .col = 5, .row = 6, .type = 3, .surface = 4, .special = 8 }, { .id = 3, .col = 6, .row = 5, .type = 1, .surface = 4, .complex = 2 }, { .id = 4, .col = 4, .row = 5, .type = 4, .surface = 4 }, { .id = 5, .col = 5, .row = 4, .type = 6, .surface = 4 }, { .id = 6, .col = 5, .row = 3, .type = 5, .surface = 4 },
    };
    world.pool_complexes = { { .index = 2, .ticket = 250, .name = "Lido", .wave_height = 5, .wave_frequency = 4, .wave_on = 45, .wave_every = 120 } };
    world.changing_rooms = { { .object_ref = 90, .segment = 3, .open = true, .fluid = 2 } };
    const std::vector<save::PlacedObject> objects { { .id = 90, .symbol = "Room", .col = 7, .row = 5 } };
    const park::PoolMap map = park::saved_pools(world, objects);
    REQUIRE(map.squares.size() == 6);
    CHECK(map.at(5, 5)->kind == park::PoolKind::Water);
    CHECK(map.at(5, 6)->kind == park::PoolKind::HighDive);
    CHECK(map.at(5, 6)->board == 8);
    CHECK(map.at(5, 6)->turn == 2); // The water is a row back
    CHECK(map.at(4, 5)->kind == park::PoolKind::Jacuzzi);
    CHECK(map.at(5, 3)->kind == park::PoolKind::Path);
    CHECK(map.at(5, 4)->kind == park::PoolKind::PathSlope);
    CHECK(map.at(5, 4)->turn == 2); // Its foot at the path a row back
    REQUIRE(map.rooms.size() == 1);
    CHECK(map.rooms[0].turn == 3); // Its paved square a column back
    CHECK(map.rooms[0].ticket == 250);
    CHECK(map.rooms[0].complex_name == "Lido");
    CHECK(map.rooms[0].open);
    CHECK(map.rooms[0].fluid == 2);
    CHECK(map.rooms[0].waves.height == 5);
    CHECK(map.rooms[0].waves.frequency == 4);
    CHECK(map.rooms[0].waves.on == 45);
    CHECK(map.rooms[0].waves.every == 120);
}
