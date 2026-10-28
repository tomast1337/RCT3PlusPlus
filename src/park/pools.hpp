// SPDX-License-Identifier: GPL-3.0-or-later
//
// Swimming pools, first expansion: the squares the player lays, the pieces each one is built of, and what
// laying them costs.
//
// A pool is laid square by square on the park's grid. A square holds water, or is paved: a tile round the
// water, or a spa pool. Each square is a stack of the pool type's pieces, one to a height unit, from `depth`
// below its surface up to the surface: a paved square is solid all the way up; a water square has a floor
// piece at the bottom and, where a side or a corner meets no water, a wall turned to face it. A square of
// the pool has a railing on each side that meets no square of the pool.
//
// Extras (sun loungers, lamps, ladders and the rest) are scenery items standing on a square, each against one
// of its sides. Each takes parts of its square, named by the words of its pool extra setting: the middle of a
// paved square or of a water square, the side it is against, the wall under that side, the water across from
// it, or the whole square (its middle and all four sides). A paved square offers its middle and its sides; a
// water square its middle, its walled sides with the walls under them, and water at its open sides. An extra
// fits where its square offers all it takes and no other extra there has taken any of it.
//
// Guests come into a pool through a changing room: a building standing on the ground beside a paved square
// of the pool, which leaves that square's railing open towards it. Squares that meet side to side make one
// pool complex; its changing rooms share its ticket price and are opened and closed together.
//
// Turns are quarter turns as placed objects count them; a piece turned by 0 has its wall on the square's
// side towards the next row, 1 towards the next column, 2 towards the row before, 3 towards the column
// before.
#pragma once

#include "save/model/world.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::park
{

    enum class PoolKind : std::uint8_t
    {
        Water,
        Tile,     ///< Paved
        Jacuzzi,  ///< A spa pool, on a paved square
        HighDive, ///< A high diving board, on a paved square, facing the water at its side `turn`
    };

    struct PoolSquare
    {
        std::int32_t col = 0;
        std::int32_t row = 0;
        PoolKind kind = PoolKind::Water;
        std::int32_t surface = 0; ///< Whole units, as placed objects' heights
        std::int32_t depth = 2;   ///< Whole units: 1 shallow, 2 normal, 3 deep
        std::int32_t board = 0;   ///< A high diving board's height over the surface, whole units
        std::int32_t turn = 0;    ///< The side a high diving board faces
    };

    /// A high diving board climbs in levels of this many units: a stretch of ladder and a board at each.
    inline constexpr std::int32_t kBoardLevel = 3;

    /// What an extra takes of its square, from the words of its pool extra setting.
    struct PoolExtraFit
    {
        bool paved_middle = false; ///< "CentralSolid"
        bool water_middle = false; ///< "CentralWater"
        bool side = false;         ///< "Side"
        bool wall = false;         ///< "UnderWaterWall"
        bool water_across = false; ///< "OppositeWater"
        bool whole = false;        ///< "WholeTile"
        std::int32_t step = 0;     ///< "HeightStep" and its number: a change of level at its side
    };

    /// The fit the words of a pool extra setting name, in any case; words it does not know are left out.
    [[nodiscard]] PoolExtraFit pool_extra_fit(std::string_view words);

    /// An extra the player has put in a pool.
    struct PoolExtra
    {
        std::string symbol { };  ///< Its scenery item
        std::string overlay { }; ///< The scenery item's overlay
        std::int32_t col = 0;
        std::int32_t row = 0;
        std::int32_t turn = 0;   ///< The side it is against
        std::int64_t refund = 0; ///< What taking it away costs, hundredths; below zero, money back
        PoolExtraFit fit { };
    };

    /// A new pool complex's ticket price, hundredths: what parks saved with a pool just built show.
    inline constexpr std::int64_t kPoolTicket = 100;

    /// A changing room the player has built beside a pool.
    struct PoolRoom
    {
        std::string symbol { };  ///< Its scenery item
        std::string overlay { }; ///< The scenery item's overlay
        std::int32_t col = 0;
        std::int32_t row = 0;
        std::int32_t turn = 0; ///< The side towards the paved square it opens on
        std::int64_t ticket = kPoolTicket;
        bool open = false;
        std::string complex_name { }; ///< Its pool complex's name, the same in every room of it; empty for the default
    };

    /// The pools the player has laid, a square to a tile, the extras in them and their changing rooms.
    struct PoolMap
    {
        std::vector<PoolSquare> squares { };
        std::vector<PoolExtra> extras { };
        std::vector<PoolRoom> rooms { };

        [[nodiscard]] std::optional<const PoolSquare&> at(std::int32_t col, std::int32_t row) const noexcept;
    };

    /// The shapes of wall a water square takes, each a set of pieces of the pool type.
    enum class PoolWall : std::uint8_t
    {
        Edge,        ///< Along one side
        OuterCorner, ///< Round two sides that meet
        InnerCorner, ///< In one corner, the two sides beside it open
        End,         ///< Round three sides
        Full,        ///< Round all four
    };

    struct PoolWallPiece
    {
        PoolWall shape = PoolWall::Edge;
        std::int32_t turn = 0; ///< A corner's turn is that of the first of its two sides, counting up
    };

    /// The sides and corners round a square, as bits of the mask pool_walls takes.
    enum PoolSide : std::uint8_t
    {
        kNextRow = 1u << 0,    ///< Turn 0
        kNextCol = 1u << 1,    ///< Turn 1
        kRowBefore = 1u << 2,  ///< Turn 2
        kColBefore = 1u << 3,  ///< Turn 3
        kNextBoth = 1u << 4,   ///< The corner between turns 0 and 1
        kRowColNext = 1u << 5, ///< Between turns 1 and 2: the next column, the row before
        kBothBefore = 1u << 6, ///< Between 2 and 3
        kColRowNext = 1u << 7, ///< Between 3 and 0: the column before, the next row
    };

    /// The walls of a water square whose neighbours holding water are the bits `water` (PoolSide).
    /// shortcut: straight edges and square corners only; the pool type's curved pieces wait for the curve
    /// modes of the pool tool.
    [[nodiscard]] std::vector<PoolWallPiece> pool_walls(std::uint8_t water) noexcept;

    /// The neighbours of `square` that hold water, at its own surface, as pool_walls takes them.
    [[nodiscard]] std::uint8_t water_around(const PoolMap& map, const PoolSquare& square) noexcept;

    struct PoolPiece
    {
        std::string_view symbol; ///< The scenery item in the pool type's overlay
        std::int32_t height = 0; ///< Whole units
        std::int32_t turn = 0;
    };

    /// The pieces `square` of `map` is built of, from the bottom up, railings last.
    [[nodiscard]] std::vector<PoolPiece> pool_pieces(const PoolMap& map, const PoolSquare& square);

    /// The pieces of every square of `map`, as placed objects of the pool type in `overlay`, each standing
    /// on its square at its own height, then the extras, at their squares' surface, then the changing rooms,
    /// at the surface of the square they open on. New objects take ids
    /// after `last_id`.
    [[nodiscard]] std::vector<save::PlacedObject> pool_objects(const PoolMap& map, std::string_view overlay, std::uint64_t last_id);

    /// The water of every water square, as the park's water mesh takes it.
    [[nodiscard]] std::vector<save::PoolWater> pool_water(const PoolMap& map);

    /// What laying `square` costs, from the pool type's price list (ovl::PoolType::prices); in hundredths.
    [[nodiscard]] std::int64_t pool_build_cost(std::span<const std::int32_t, 18> prices, const PoolSquare& square) noexcept;
    /// What taking `square` up again costs: below zero, money back.
    [[nodiscard]] std::int64_t pool_refund(std::span<const std::int32_t, 18> prices, const PoolSquare& square) noexcept;

    /// Whether an extra that fits as `fit` may go in `map` on the square at (`col`, `row`), against its side
    /// `turn`.
    [[nodiscard]] bool pool_extra_fits(const PoolMap& map, std::int32_t col, std::int32_t row, const PoolExtraFit& fit, std::int32_t turn);

    /// Whether a changing room may stand at (`col`, `row`), off the pool, opening on the paved square at its
    /// side `turn`, where no other room opens.
    [[nodiscard]] bool pool_room_fits(const PoolMap& map, std::int32_t col, std::int32_t row, std::int32_t turn);

    /// The squares of the complex the room `room` (of map.rooms) opens on: every square joined to the one it
    /// opens on.
    [[nodiscard]] std::vector<save::Tile> pool_complex(const PoolMap& map, std::size_t room);

    /// The changing rooms of the complex the room `room` (of map.rooms) opens on, itself among them.
    [[nodiscard]] std::vector<std::size_t> pool_complex_rooms(const PoolMap& map, std::size_t room);

    /// Lays `squares` in `map`, each over whatever square was where it goes, and returns what that costs:
    /// each new square's price and the refund of each it takes the place of, and of the extras on it. A square
    /// laid over one the same costs nothing. Changing rooms whose square is no longer paved, or that now stand
    /// in the pool, go.
    std::int64_t lay_pool(PoolMap& map, std::span<const std::int32_t, 18> prices, std::span<const PoolSquare> squares);

    /// Takes up the squares of `map` from `a` to `b`, the extras on them and the changing rooms opening on
    /// them; returns the refund, below zero.
    std::int64_t take_up_pool(PoolMap& map, std::span<const std::int32_t, 18> prices, save::Tile a, save::Tile b);

} // namespace rct3::park
