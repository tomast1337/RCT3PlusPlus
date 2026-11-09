// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/design_placement.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{

    /// A pool of 4 by 4 water squares with its surface at height 6, its corner at (10, 10).
    pools::PoolMap pool()
    {
        pools::PoolMap map;
        for (std::int32_t row = 10; row < 14; ++row)
            for (std::int32_t col = 10; col < 14; ++col)
                map.squares.push_back({ .col = col, .row = row, .surface = 6 });
        return map;
    }

} // namespace

TEST_CASE("design placement: each refusal names its own text, and none names none")
{
    CHECK(refusal_key(DesignRefusal::None).empty());
    CHECK(refusal_key(DesignRefusal::NotOwned) == "PlaceObject_NotOwned");
    CHECK(refusal_key(DesignRefusal::TooHigh) == "PlaceObject_TooHigh");
    CHECK(refusal_key(DesignRefusal::Intersects) == "PlaceObject_Intersects");
    CHECK(refusal_key(DesignRefusal::IntoGround) == "PlaceObject_IntersectsGround");
    CHECK(refusal_key(DesignRefusal::OffPoolEdge) == "PlaceObject_MustEndOnPoolEdge");
}

TEST_CASE("design placement: a design spans a gentle dip but not a deep valley")
{
    const std::array level { 20.0f, 20.0f, 20.0f };
    CHECK_FALSE(stands_too_high(20, level));
    const std::array dip { 20.0f, 20.0f - kDesignRaise, 20.0f };
    CHECK_FALSE(stands_too_high(20, dip));
    const std::array valley { 20.0f, 20.0f - kDesignRaise - 1, 20.0f };
    CHECK(stands_too_high(20, valley));
    // Higher ground under a piece never makes it too high: its supports are only shorter.
    const std::array hill { 20.0f, 40.0f };
    CHECK_FALSE(stands_too_high(20, hill));
}

TEST_CASE("design placement: a piece's pool sides turn and move with it")
{
    PieceRecord record;
    record.squares = { 1, 2 };
    record.pool_sides = { { .x = 0, .z = 1, .height = 2, .side = Side::kNextRow } };
    const save::PlacedObject unturned { .col = 5, .row = 7, .base_height = 4 };
    const std::vector<pools::PoolOpening> straight = piece_pool_openings(unturned, record);
    REQUIRE(straight.size() == 1);
    CHECK(straight[0].height == 6);
    CHECK(straight[0].side == Side::kNextRow);
    save::PlacedObject turned = unturned;
    turned.rotation = 1;
    const std::vector<pools::PoolOpening> quarter = piece_pool_openings(turned, record);
    REQUIRE(quarter.size() == 1);
    CHECK(quarter[0].side == Side::kNextCol);
    CHECK(quarter[0].height == 6);
}

TEST_CASE("design placement: pool sides must each face a pool square at their height")
{
    const pools::PoolMap map = pool();
    // Beside the pool's lower row edge, facing into it.
    const std::array edge { pools::PoolOpening { .col = 11, .row = 9, .height = 6, .side = Side::kNextRow } };
    CHECK(meets_pools(map, edge));
    // Facing away from it.
    const std::array away { pools::PoolOpening { .col = 11, .row = 9, .height = 6, .side = Side::kRowBefore } };
    CHECK_FALSE(meets_pools(map, away));
    // At the right place but over the water.
    const std::array high { pools::PoolOpening { .col = 11, .row = 9, .height = 8, .side = Side::kNextRow } };
    CHECK_FALSE(meets_pools(map, high));
    // One opening met and one not is still refused; none at all needs no pool.
    const std::array half { edge[0], away[0] };
    CHECK_FALSE(meets_pools(map, half));
    CHECK(meets_pools(map, std::span<const pools::PoolOpening> { }));
}
