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
// The ground is dug down to a square's floor where it is higher, and left where it is lower: a raised pool stands
// over it on the support columns its pieces ask for. A raised square may stand over another on the same tile.
//
// Where two walls meet round the outside of a corner, the square's edge setting, chosen when it was laid,
// shapes the corner: square, cut across, or a tight curve inside the square. A loose curve sweeps on into the
// squares on either side, which take curved walls to meet it; it falls back to a tight curve where a square
// beside it has walls laid straight or cut across, or is a lone square or the end of a strip, and where the corner
// square has a corner where water goes round the inside as well. Squares laid curved round the ends of strips and
// lone squares too. A corner where water goes round the inside has a small rounded wall, a plain one where a corner
// cut across meets it, and a curved one to meet a sweep beside it.
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
// Sides of a square are counted as park::Side counts them. A pool piece's sides turn with it: turned by 0
// (ModelTurn) it has its wall on the square's side towards the next row (side 0), turned by 1 towards the next
// column, and so on.
#pragma once

#include "park/footpath_builder.hpp"
#include "park/turns.hpp"
#include "save/model/world.hpp"

#include <array>
#include <cstdint>
#include <limits>
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
        HighDive, ///< A high diving board, on a paved square, facing the water at its side `side`
        Stairs,   ///< A spiral staircase, on a paved square, climbing `board` units from its foot at side `side`
        // Pool paths: walkways of one piece, no deeper than their surface, joining the paths beside them.
        Path,
        PathSlope,  ///< Rising kPathSlopeRise units from its foot at side `side`, at the surface
        PathStairs, ///< Rising kPathStairsRise units, the same way
    };

    /// Whether `kind` is a pool path's.
    [[nodiscard]] constexpr bool is_pool_path(PoolKind kind) noexcept
    {
        return kind == PoolKind::Path || kind == PoolKind::PathSlope || kind == PoolKind::PathStairs;
    }
    inline constexpr std::int32_t kPathSlopeRise = 1;
    inline constexpr std::int32_t kPathStairsRise = 2;

    /// How a square's walls go round its corners, as the pool tool's edge buttons set it.
    enum class PoolEdge : std::uint8_t
    {
        Straight,
        Diagonal, ///< Cut across
        Tight,    ///< A curve inside the corner square
        Loose,    ///< A wide curve, sweeping on into the squares either side
    };

    struct PoolSquare
    {
        std::int32_t col = 0;
        std::int32_t row = 0;
        PoolKind kind = PoolKind::Water;
        std::int32_t surface = 0; ///< Whole units, as placed objects' heights
        std::int32_t depth = 2;   ///< Whole units: 1 shallow, 2 normal, 3 deep
        std::int32_t board = 0;   ///< A high diving board's or a staircase's height over the surface, whole units
        Side side { };            ///< The side a high diving board faces, or a staircase's or a slope's foot
        PoolEdge edge = PoolEdge::Straight;
        /// The ground's corners, as the grid keeps them, before the square dug it down to its floor: put back when
        /// it goes. None where the ground was under its floor, as under a raised pool, which stands over it.
        std::optional<std::array<float, 4>> ground { };
    };

    /// A pool square stacked over another on one tile leaves at least this much between the lower one's top and
    /// the upper one's floor; one laid closer takes the other's place.
    inline constexpr std::int32_t kPoolHeadroom = 3; ///< Chosen by eye: room for a person on the lower deck

    /// A pool path piece as a trail lays it, and the height it is left at.
    struct PoolPathPiece
    {
        PoolSquare square;
        std::int32_t end = 0;
    };

    /// The pool path piece `builder` (park/footpath_builder.hpp) lays next: on the square after its last step,
    /// in its heading, entered at the height that step ended at (its start height when it has none). Flat it is
    /// a path; a slope or steps have their foot on the side it is entered by going up, and on the side it is left
    /// by going down. None before the trail has a start.
    [[nodiscard]] std::optional<PoolPathPiece> next_pool_path(const FootpathBuilder& builder);

    /// A high diving board climbs in levels of this many units: a stretch of ladder and a board at each.
    inline constexpr std::int32_t kBoardLevel = 3;
    /// A spiral staircase climbs at least this many units: its foot, its top, and a turn between.
    inline constexpr std::int32_t kLowestStairs = 3;

    /// What an extra takes of its square, from the words of its pool extra setting.
    struct PoolExtraFit
    {
        bool paved_middle = false; ///< "CentralSolid"
        bool water_middle = false; ///< "CentralWater"
        bool side = false;         ///< "Side"
        bool wall = false;         ///< "UnderWaterWall"
        bool water_across = false; ///< "OppositeWater"
        bool whole = false;        ///< "WholeTile"
        bool lifeguard = false;    ///< "LifeGuard": a lifeguard's chair, which a lifeguard comes with
        bool lounger = false;      ///< "SunLounger": swimmers lie on it
        bool shower = false;       ///< "Shower": swimmers stand under it
        bool board = false;        ///< "DivingBoard": swimmers walk out on it and dive off
        bool ladder = false;       ///< "Ladder": out of the water up its wall; swimmers climb out by it
        bool light = false;        ///< "Light": a lamp, on the paving or in a wall under the water
        bool wave_machine = false; ///< "WaveMachine": it makes waves in the pool, out from its wall
        std::int32_t step = 0;     ///< "HeightStep" and its number: a change of level at its side
    };

    /// The fit the words of a pool extra setting name, in any case; words it does not know are left out.
    [[nodiscard]] PoolExtraFit pool_extra_fit(std::string_view words);

    /// An extra the player has put in a pool.
    /// shortcut: on a tile with squares stacked it stands on the highest; keep its own level once extras go under a
    /// raised pool.
    struct PoolExtra
    {
        std::string symbol { };  ///< Its scenery item
        std::string overlay { }; ///< The scenery item's overlay
        std::int32_t col = 0;
        std::int32_t row = 0;
        Side side { };           ///< The side it is against
        std::int64_t refund = 0; ///< What taking it away costs, hundredths; below zero, money back
        PoolExtraFit fit { };
    };

    /// A new pool complex's ticket price, hundredths: what parks saved with a pool just built show.
    inline constexpr std::int64_t kPoolTicket = 100;

    /// How a pool complex's wave machines run, the same in every changing room of it. The machines push the water
    /// up and down `frequency` times in 2 pi seconds, as high as `height` says, for `on` seconds of every `every`.
    struct PoolWaves
    {
        float height = 8;    ///< Of the crests, in steps from kLeastWaveHeight to kMostWaveHeight
        float frequency = 6; ///< Radians a second; the fewer, the longer the waves
        float on = 30;       ///< Seconds the machines run for
        float every = 60;    ///< Seconds from one start of the machines to the next
    };
    inline constexpr float kLeastWaveHeight = 2;    ///< Chosen by eye: the lowest the wave height setting goes
    inline constexpr float kMostWaveHeight = 14;    ///< Chosen by eye: the highest
    inline constexpr float kLeastWaveFrequency = 2; ///< Chosen by eye: the longest waves
    inline constexpr float kMostWaveFrequency = 10; ///< Chosen by eye: the shortest

    /// A changing room the player has built beside a pool.
    struct PoolRoom
    {
        std::string symbol { };  ///< Its scenery item
        std::string overlay { }; ///< The scenery item's overlay
        std::int32_t col = 0;
        std::int32_t row = 0;
        Side side { }; ///< The side towards the paved square it opens on
        std::int64_t ticket = kPoolTicket;
        bool open = false;
        std::string complex_name { }; ///< Its pool complex's name, the same in every room of it; empty for the default
        std::uint8_t fluid = 0;       ///< The cleaning fluid it keeps its pool clean with, mildest 0 to harshest 2
        PoolWaves waves { };
    };

    /// The pools the player has laid, the extras in them and their changing rooms. A tile holds a square, or several
    /// stacked with headroom between them.
    struct PoolMap
    {
        std::vector<PoolSquare> squares { };
        std::vector<PoolExtra> extras { };
        std::vector<PoolRoom> rooms { };

        /// The square at (`col`, `row`) whose surface is nearest `level`; without one, the highest, as seen from
        /// above.
        [[nodiscard]] std::optional<const PoolSquare&> at(std::int32_t col, std::int32_t row, std::int32_t level = std::numeric_limits<std::int32_t>::max()) const noexcept;
    };

    /// The height a pool path trail starts at from side `side` of `square`, a square of `map`: its surface, or a
    /// slope's or steps' foot or top, which are the only sides they start from. None where a square beside already
    /// meets that side at that height, so trails start only at the complex's edges, water and paving alike.
    [[nodiscard]] std::optional<std::int32_t> pool_path_start(const PoolMap& map, const PoolSquare& square, Side side);

    /// A square of a pool slide's station platform, where swimmers get on.
    struct SlidePlatform
    {
        std::int32_t col = 0;
        std::int32_t row = 0;
        std::int32_t height = 0; ///< Of its deck, whole units
        ModelTurn turn { };      ///< Its piece's turn: the side it brings the piece's side 0 to, and the opposite one, are off its track
    };

    /// The height a pool path trail starts at from side `side` of `platform`: its deck's, from the two sides off its
    /// track, as saved parks join pool paths to their slides' stations. None where a square of `map` already meets
    /// that side there.
    [[nodiscard]] std::optional<std::int32_t> slide_path_start(const PoolMap& map, const SlidePlatform& platform, Side side);

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
        Side side { }; ///< The side it is along; a corner's is the first of its two sides, clockwise
    };

    /// The sides and corners round a square, as bits of the mask pool_walls takes.
    enum PoolSide : std::uint8_t
    {
        kNextRow = 1u << 0,    ///< Side 0
        kNextCol = 1u << 1,    ///< Side 1
        kRowBefore = 1u << 2,  ///< Side 2
        kColBefore = 1u << 3,  ///< Side 3
        kNextBoth = 1u << 4,   ///< The corner between sides 0 and 1
        kRowColNext = 1u << 5, ///< Between sides 1 and 2: the next column, the row before
        kBothBefore = 1u << 6, ///< Between 2 and 3
        kColRowNext = 1u << 7, ///< Between 3 and 0: the column before, the next row
    };

    /// The walls of a water square whose neighbours holding water are the bits `water` (PoolSide), whatever
    /// its edge setting.
    [[nodiscard]] std::vector<PoolWallPiece> pool_walls(std::uint8_t water) noexcept;

    /// The neighbours of `square` that hold water, at its own surface, as pool_walls takes them.
    [[nodiscard]] std::uint8_t water_around(const PoolMap& map, const PoolSquare& square) noexcept;

    /// Whether the point (`x`, `z`) of the water square `square` of `map`, in tiles from its middle along the
    /// columns and the rows, lies over open water rather than the paving round it. Each wall's coping takes the
    /// quarter tile along its side, and a corner where water goes round the inside a quarter tile round the corner.
    /// An outside corner is square, cut across from where one coping meets the open side to where the other does,
    /// or rounded: a tight curve as wide as the water reaches past it, a strip's end in a half circle and a lone
    /// square in a whole one. A loose curve that sweeps keeps its water inside a circle that runs through its
    /// corner square and the squares either side.
    [[nodiscard]] bool pool_water_at(const PoolMap& map, const PoolSquare& square, float x, float z);

    struct PoolPiece
    {
        std::string_view symbol; ///< The scenery item in the pool type's overlay
        std::int32_t height = 0; ///< Whole units
        ModelTurn turn { };
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

    /// The pools a park from the game keeps, as squares and changing rooms the pool tool can work on: water,
    /// paving, spa pools, high diving boards, spiral staircases and pool paths, and the rooms among `objects` that
    /// open on them.
    /// shortcut: the extras stay the scenery they are placed as; take them in once the pool tool lays them.
    [[nodiscard]] PoolMap saved_pools(const save::WorldModel& world, std::span<const save::PlacedObject> objects);

    /// Takes out of `objects` the pieces a park from the game placed for `map`'s squares, so the squares alone place
    /// them and the pool tool's work redraws them: those in the pool type in `overlay` (named with either slash, in
    /// any case) that pool_objects lays for `map`. The rest stays as placed: the extras, and any piece it never lays.
    void drop_saved_pool_pieces(const PoolMap& map, std::string_view overlay, std::vector<save::PlacedObject>& objects);

    /// Digs the ground of `grid` down to the floor of each square of `map` where it is higher, keeping in the square
    /// the ground it dug; the ground under a raised square is left as it is.
    void dig_under_pools(PoolMap& map, save::TerrainGrid& grid);

    /// What laying `square` costs, from the pool type's price list (ovl::PoolType::prices); in hundredths.
    [[nodiscard]] std::int64_t pool_build_cost(std::span<const std::int32_t, 18> prices, const PoolSquare& square) noexcept;
    /// What taking `square` up again costs: below zero, money back.
    [[nodiscard]] std::int64_t pool_refund(std::span<const std::int32_t, 18> prices, const PoolSquare& square) noexcept;

    /// Whether an extra that fits as `fit` may go in `map` on the square at (`col`, `row`), against its side
    /// `side`.
    [[nodiscard]] bool pool_extra_fits(const PoolMap& map, std::int32_t col, std::int32_t row, const PoolExtraFit& fit, Side side);

    /// Whether a changing room may stand at (`col`, `row`), off the pool, opening on the paved square at its
    /// side `side`, where no other room opens.
    [[nodiscard]] bool pool_room_fits(const PoolMap& map, std::int32_t col, std::int32_t row, Side side);

    /// The squares of the complex the room `room` (of map.rooms) opens on: every square joined to the one it
    /// opens on.
    [[nodiscard]] std::vector<save::Tile> pool_complex(const PoolMap& map, std::size_t room);

    /// The changing rooms of the complex the room `room` (of map.rooms) opens on, itself among them.
    [[nodiscard]] std::vector<std::size_t> pool_complex_rooms(const PoolMap& map, std::size_t room);

    /// Lays `squares` in `map`, each over whatever square was where it goes, and returns what that costs:
    /// each new square's price and the refund of each it takes the place of, and of the extras on it. A square
    /// laid over one the same, but for its edge setting, costs nothing. A square laid above or below one on its tile,
    /// with kPoolHeadroom between, is stacked with it. Changing rooms whose square is no longer paved, or that now stand
    /// in the pool, go.
    std::int64_t lay_pool(PoolMap& map, std::span<const std::int32_t, 18> prices, std::span<const PoolSquare> squares);

    /// Takes up the squares of `map` from `a` to `b`, the extras on them and the changing rooms opening on
    /// them; returns the refund, below zero. Given a `level`, of squares stacked on a tile only the one nearest it.
    std::int64_t take_up_pool(PoolMap& map, std::span<const std::int32_t, 18> prices, save::Tile a, save::Tile b, std::optional<std::int32_t> level = std::nullopt);

} // namespace rct3::park
