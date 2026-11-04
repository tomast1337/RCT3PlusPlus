// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/pool_score.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3::park;

namespace
{
    /// `cols` by `rows` squares of water at depth `depth`, corner at (0, 0).
    std::vector<PoolSquare> water(std::int32_t cols, std::int32_t rows, std::int32_t depth = 2)
    {
        std::vector<PoolSquare> out;
        for (std::int32_t row = 0; row < rows; ++row)
            for (std::int32_t col = 0; col < cols; ++col)
                out.push_back({ .col = col, .row = row, .depth = depth });
        return out;
    }

    PoolExtra extra(std::int32_t col, std::int32_t row, PoolExtraFit fit)
    {
        return { .col = col, .row = row, .fit = fit };
    }
} // namespace

TEST_CASE("a pool's tally counts its water, shaped corners, features and the extras on its squares only")
{
    auto squares = water(3, 3);
    squares[4].edge = PoolEdge::Tight; // The middle one, the only shaped square not at a corner
    squares.push_back({ .col = 3, .row = 1, .kind = PoolKind::Jacuzzi });
    const std::vector extras { extra(0, 0, { .ladder = true }), extra(3, 1, { .lounger = true }), extra(9, 9, { .light = true }) };
    const PoolTally t = pool_tally(squares, extras);
    CHECK(t.squares == 10);
    CHECK(t.water == 9);
    CHECK(t.shaped == 5); // Four corners and the curved one
    CHECK(t.jacuzzis == 1);
    CHECK(t.ladders == 1);
    CHECK(t.loungers == 1);
    CHECK(t.lamps == 0);
}

TEST_CASE("more water, mixed depths, lamps, ladders and lifeguards each make a pool more exciting, up to 1")
{
    const PoolTally plain = pool_tally(water(4, 4), { });
    const float base = pool_excitement(plain, 0);
    CHECK(base > 0);
    CHECK(pool_excitement(pool_tally(water(8, 8), { }), 0) > base);
    auto deep = water(4, 4);
    for (std::size_t i = 0; i < 8; ++i)
        deep[i].depth = 3;
    CHECK(pool_excitement(pool_tally(deep, { }), 0) > base);
    for (const PoolExtraFit fit : { PoolExtraFit { .ladder = true }, PoolExtraFit { .light = true }, PoolExtraFit { .lifeguard = true } })
        CHECK(pool_excitement(pool_tally(water(4, 4), std::vector { extra(1, 1, fit) }), 0) > base);
    PoolTally full = plain;
    full.water = full.squares = 1000;
    full.shaped = full.ladders = full.lifeguards = full.lamps = 1000;
    full.depth_sum = 2000;
    full.depth_sum2 = 5000;
    CHECK(pool_excitement(full, 0) == doctest::Approx(1));
}

TEST_CASE("dirt takes a pool's excitement down, to half when it is a health hazard")
{
    const PoolTally t = pool_tally(water(6, 6), { });
    CHECK(pool_excitement(t, 0.5f) < pool_excitement(t, 0));
    CHECK(pool_excitement(t, 1) == doctest::Approx(pool_excitement(t, 0) / 2));
}

TEST_CASE("a pool's worth grows with its features, each kind counting only up to a few")
{
    const PoolTally plain = pool_tally(water(4, 4), { });
    const auto worth = pool_worth(plain, 0, 0);
    CHECK(worth >= 100); // A new complex's ticket still sells
    CHECK(pool_worth(plain, 1, 0) > worth);
    CHECK(pool_worth(plain, 9, 0) == pool_worth(plain, 4, 0));
    PoolTally spa = plain;
    spa.jacuzzis = 1;
    CHECK(pool_worth(spa, 0, 0) > worth);
    spa.jacuzzis = 50;
    PoolTally four = plain;
    four.jacuzzis = 4;
    CHECK(pool_worth(spa, 0, 0) == pool_worth(four, 0, 0));
    CHECK(pool_worth(plain, 0, 1) < worth);
}
