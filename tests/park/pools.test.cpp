// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/pools.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{

    /// An invented price list, shaped like a pool type's.
    constexpr std::array<std::int32_t, 18> kPrices { 0, 0, 100, 300, 50, -200, 70, 10, -60, -5, 0, 0, 900, -800, 0, 0, 0, 0 };

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

TEST_CASE("pools: an extra's words name what it takes of its square")
{
    const PoolExtraFit fit = pool_extra_fit(" side centralsolid WholeTile HeightStep 2 Shower");
    CHECK(fit.side);
    CHECK(fit.paved_middle);
    CHECK(fit.whole);
    CHECK(fit.step == 2);
    CHECK_FALSE(fit.wall);
    CHECK_FALSE(fit.water_middle);
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
