// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/pools.hpp"

#include "strings/ascii.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <set>

namespace rct3::park
{

    namespace
    {

        /// A wall of the pool type, the names of its pieces from the bottom of a stack up.
        using WallStack = std::array<std::string_view, 3>;
        constexpr WallStack kEdge { "EdgeBot", "EdgeMid", "EdgeTop" };
        constexpr WallStack kSquareCorner { "Corner1Bot", "Corner1Mid", "Corner1Top" };
        constexpr WallStack kCutCorner { "Corner2Bot", "Corner2Mid", "Corner2Top" };
        constexpr WallStack kPlainInside { "Corner3Bot", "Corner3Mid", "Corner3Top" };
        constexpr WallStack kEnd { "EndBot", "EndMid", "EndTop" };
        constexpr WallStack kFull { "FullBot", "FullMid", "FullTop" };
        constexpr WallStack kCurvedEnd { "CurveEndBot", "CurveEndMid", "CurveEndTop" };
        constexpr WallStack kCurvedFull { "CurveFullBot", "CurveFullMid", "CurveFullTop" };
        constexpr WallStack kSweep { "CurveCorner9Bot", "CurveCorner9Mid", "CurveCorner9Top" };
        /// An edge meeting a sweep: at its next side, at its side before, at both.
        constexpr std::array<WallStack, 3> kSweptEdge { {
            { "CurveEdge1Bot", "CurveEdge1Mid", "CurveEdge1Top" },
            { "CurveEdge2Bot", "CurveEdge2Mid", "CurveEdge2Top" },
            { "CurveEdge3Bot", "CurveEdge3Mid", "CurveEdge3Top" },
        } };
        /// A tight curve, alone, then meeting a sweep at its side before, at its side after next, at both.
        constexpr std::array<WallStack, 4> kTightCorner { {
            { "CurveCorner5Bot", "CurveCorner5Mid", "CurveCorner5Top" },
            { "CurveCorner7Bot", "CurveCorner7Mid", "CurveCorner7Top" },
            { "CurveCorner6Bot", "CurveCorner6Mid", "CurveCorner6Top" },
            { "CurveCorner8Bot", "CurveCorner8Mid", "CurveCorner8Top" },
        } };
        /// A rounded inside corner, alone, then meeting a sweep at its side, at its side before, at both.
        constexpr std::array<WallStack, 4> kInside { {
            { "CurveCorner1Bot", "CurveCorner1Mid", "CurveCorner1Top" },
            { "CurveCorner2Bot", "CurveCorner2Mid", "CurveCorner2Top" },
            { "CurveCorner3Bot", "CurveCorner3Mid", "CurveCorner3Top" },
            { "CurveCorner4Bot", "CurveCorner4Mid", "CurveCorner4Top" },
        } };

        /// What one square costs to lay or to take up, from the pool type's price list
        /// (ovl::PoolType::prices, by index): a part for its kind, another for each unit of its depth, more
        /// for a spa pool, more for each unit a high diving board climbs, in whole levels, and more for each
        /// unit a staircase climbs.
        struct PriceFields
        {
            std::size_t base, per_depth, jacuzzi, board, stairs;
        };
        constexpr PriceFields kBuild[] { { 2, 3, 0, 0, 0 }, { 6, 7, 12, 16, 14 }, { 10, 0, 0, 0, 0 } };  // Water, paved, paths
        constexpr PriceFields kRefund[] { { 4, 5, 0, 0, 0 }, { 8, 9, 13, 17, 15 }, { 11, 0, 0, 0, 0 } }; // The same, when taken up

        std::int64_t price(std::span<const std::int32_t, 18> prices, const PoolSquare& square, const PriceFields (&fields)[3]) noexcept
        {
            const PriceFields& f = fields[square.kind == PoolKind::Water ? 0 : is_pool_path(square.kind) ? 2 : 1];
            const std::int64_t climb = square.kind == PoolKind::HighDive ? square.board / kBoardLevel * kBoardLevel : 0;
            const std::int64_t stairs = square.kind == PoolKind::Stairs ? square.board : 0;
            return prices[f.base] + std::int64_t { prices[f.per_depth] } * square.depth + (square.kind == PoolKind::Jacuzzi ? prices[f.jacuzzi] : 0) + climb * prices[f.board] + stairs * prices[f.stairs];
        }

        bool same(const PoolSquare& a, const PoolSquare& b) noexcept
        {
            return a.kind == b.kind && a.depth == b.depth && a.surface == b.surface && a.board == b.board && a.side == b.side;
        }

        /// Whether `a` and `b`, on one tile, leave kPoolHeadroom between the top of the lower and the floor of the
        /// upper: a staircase's or a high diving board's top is its climb over the surface, a slope's or steps' their
        /// rise.
        bool stacked(const PoolSquare& a, const PoolSquare& b) noexcept
        {
            const auto top = [](const PoolSquare& s) { return s.surface + s.board + (s.kind == PoolKind::PathSlope ? kPathSlopeRise : s.kind == PoolKind::PathStairs ? kPathStairsRise : 0); };
            return a.surface - a.depth >= top(b) + kPoolHeadroom || b.surface - b.depth >= top(a) + kPoolHeadroom;
        }

        /// The parts of a square, as bits: its middle, paved or water, then four bits each, by side, for its
        /// sides, the walls under them, water at them and changes of level at them.
        constexpr std::uint32_t kPavedMiddle = 1u << 0;
        constexpr std::uint32_t kWaterMiddle = 1u << 1;
        constexpr std::uint32_t kSideAt = 2; // The first bit of each four
        constexpr std::uint32_t kWalls = 6;
        constexpr std::uint32_t kWaterAt = 10;
        constexpr std::uint32_t kStepAt = 14;

        std::uint32_t at_side(std::uint32_t first, Side side) noexcept
        {
            return side.bit() << first;
        }

        /// The parts an extra that fits as `fit`, against side `side`, takes.
        std::uint32_t taken(const PoolExtraFit& fit, Side side) noexcept
        {
            std::uint32_t parts = (fit.paved_middle ? kPavedMiddle : 0u) | (fit.water_middle ? kWaterMiddle : 0u);
            if (fit.side)
                parts |= at_side(kSideAt, side) | (fit.step > 0 ? at_side(kStepAt, side) : 0u);
            if (fit.wall)
                parts |= at_side(kWalls, side);
            if (fit.water_across)
                parts |= at_side(kWaterAt, side.opposite());
            if (fit.whole)
                parts |= kPavedMiddle | (0xfu << kSideAt);
            return parts;
        }

        /// The parts `square` offers extras.
        std::uint32_t offered(const PoolMap& map, const PoolSquare& square) noexcept
        {
            // A paved square offers a change of level at each side where nothing beside it stands as high or lower,
            // so a ramp goes up to a higher square, or off the pool, as the game's parks have them.
            if (square.kind == PoolKind::Tile)
            {
                std::uint32_t parts = kPavedMiddle | (0xfu << kSideAt);
                for (const Side side : kSides)
                {
                    const auto [dc, dr] = side.step();
                    if (const auto next = map.at(square.col + dc, square.row + dr, square.surface); !next || next->surface > square.surface)
                        parts |= at_side(kStepAt, side);
                }
                return parts;
            }
            if (square.kind != PoolKind::Water)
                return 0;
            const std::uint8_t water = water_around(map, square);
            std::uint32_t parts = kWaterMiddle;
            for (const Side side : kSides)
                parts |= (water & side.bit()) != 0 ? at_side(kWaterAt, side) : at_side(kSideAt, side) | at_side(kWalls, side) | at_side(kStepAt, side);
            return parts;
        }

        /// Takes the extras on the square at (`col`, `row`) out of `map`; returns their refunds.
        std::int64_t clear_extras(PoolMap& map, std::int32_t col, std::int32_t row)
        {
            std::int64_t cost = 0;
            std::erase_if(map.extras, [&](const PoolExtra& e) {
                if (e.col != col || e.row != row)
                    return false;
                cost += e.refund;
                return true;
            });
            return cost;
        }

        /// The height of side `side` of `s`; a slope or steps have only their foot and top.
        std::optional<std::int32_t> side_height(const PoolSquare& s, Side side)
        {
            if (s.kind != PoolKind::PathSlope && s.kind != PoolKind::PathStairs)
                return s.surface;
            if (side == s.side)
                return s.surface;
            if (side == s.side.opposite())
                return s.surface + (s.kind == PoolKind::PathSlope ? kPathSlopeRise : kPathStairsRise);
            return std::nullopt;
        }

        /// Whether a square beside (`col`, `row`) at side `side` meets that side at `height`.
        bool met(const PoolMap& map, std::int32_t col, std::int32_t row, Side side, std::int32_t height)
        {
            const auto [dc, dr] = side.step();
            const auto next = map.at(col + dc, row + dr, height);
            return next && side_height(*next, side.opposite()) == height;
        }

        /// The water square at side `side` of `square`, at its surface.
        std::optional<const PoolSquare&> water_beside(const PoolMap& map, const PoolSquare& square, Side side)
        {
            const auto [dc, dr] = side.step();
            const auto s = map.at(square.col + dc, square.row + dr, square.surface);
            if (!s || s->kind != PoolKind::Water || s->surface != square.surface)
                return std::nullopt;
            return s;
        }

        bool has_wall(const std::vector<PoolWallPiece>& walls, PoolWall shape)
        {
            return std::ranges::contains(walls, shape, &PoolWallPiece::shape);
        }

        /// Whether `square` is the corner of a loose curve that sweeps on into the squares beside it: its only wall
        /// that corner, and none of the squares beside it walled straight or cut across, a lone square or the end of
        /// a strip.
        bool sweeps(const PoolMap& map, std::optional<const PoolSquare&> square)
        {
            if (!square || square->edge != PoolEdge::Loose)
                return false;
            if (const auto walls = pool_walls(water_around(map, *square)); walls.size() != 1 || walls[0].shape != PoolWall::OuterCorner)
                return false;
            for (const Side side : kSides)
                if (const auto next = water_beside(map, *square, side))
                {
                    const auto walls = pool_walls(water_around(map, *next));
                    const bool angular = next->edge == PoolEdge::Straight || next->edge == PoolEdge::Diagonal;
                    if (has_wall(walls, PoolWall::End) || has_wall(walls, PoolWall::Full) || (angular && (has_wall(walls, PoolWall::Edge) || has_wall(walls, PoolWall::OuterCorner))))
                        return false;
                }
            return true;
        }

        /// Whether `square` has an outside corner cut across.
        bool cut_cornered(const PoolMap& map, std::optional<const PoolSquare&> square)
        {
            return square && square->edge == PoolEdge::Diagonal && has_wall(pool_walls(water_around(map, *square)), PoolWall::OuterCorner);
        }

        /// The wall stack for `wall` of the water square `square`, and the side its pieces are turned to.
        std::pair<WallStack, Side> wall_stack(const PoolMap& map, const PoolSquare& square, const PoolWallPiece& wall)
        {
            const Side t = wall.side;
            const auto swept = [&](Side side) { return sweeps(map, water_beside(map, square, side)); };
            const bool curved = square.edge == PoolEdge::Tight || square.edge == PoolEdge::Loose;
            // Which of two sides meet a sweep, as an index: none 0, the first 1, the second 2, both 3.
            const auto meeting = [&](Side a, Side b) { return static_cast<std::size_t>((swept(a) ? 1 : 0) + (swept(b) ? 2 : 0)); };
            switch (wall.shape)
            {
                case PoolWall::Edge:
                {
                    const std::size_t at = meeting(t.right(), t.left());
                    return { at == 0 ? kEdge : kSweptEdge[at - 1], t };
                }
                case PoolWall::OuterCorner:
                    if (sweeps(map, square))
                        return { kSweep, t };
                    if (square.edge == PoolEdge::Straight)
                        return { kSquareCorner, t };
                    if (square.edge == PoolEdge::Diagonal)
                        return { kCutCorner, t };
                    return { kTightCorner[meeting(t.left(), t.opposite())], t };
                case PoolWall::InnerCorner:
                {
                    // Turned to the first of the two sides whose water meets round it; the pieces meeting a sweep at
                    // its second side alone or at both are made a turn on, as saved parks show.
                    if (const std::size_t at = meeting(t.right(), t); at != 0)
                        return { kInside[at], at == 2 ? t : t.right() };
                    const bool cut_meets = cut_cornered(map, square) || cut_cornered(map, water_beside(map, square, t)) || cut_cornered(map, water_beside(map, square, t.right()));
                    return { cut_meets ? kPlainInside : kInside[0], t };
                }
                case PoolWall::End:
                    return { curved ? kCurvedEnd : kEnd, t };
                case PoolWall::Full:
                    return { curved ? kCurvedFull : kFull, t };
            }
            return { kEdge, t };
        }

        /// Whether one of `openings` joins `square` at its side `side`, at its surface.
        bool opened(std::span<const PoolOpening> openings, const PoolSquare& square, Side side)
        {
            const auto [dc, dr] = side.step();
            return std::ranges::any_of(openings, [&](const PoolOpening& o) { return o.col == square.col + dc && o.row == square.row + dr && o.side == side.opposite() && o.height == square.surface; });
        }

        /// Whether something meets `square`, a path, at its side `side`, at its surface: one of `openings` (a slide's
        /// station or end), a square of the complex there level with it, the foot or the top of a slope or of steps
        /// facing it, or the foot or the top of a spiral staircase.
        bool path_joins(const PoolMap& map, const PoolSquare& square, Side side, std::span<const PoolOpening> openings)
        {
            if (opened(openings, square, side))
                return true;
            const auto [dc, dr] = side.step();
            const auto next = map.at(square.col + dc, square.row + dr, square.surface);
            if (!next)
                return false;
            if (next->kind == PoolKind::Stairs)
                return next->surface == square.surface || next->surface + std::max(next->board, kLowestStairs) == square.surface;
            if (next->kind != PoolKind::PathSlope && next->kind != PoolKind::PathStairs)
                return next->surface == square.surface;
            const std::int32_t rise = next->kind == PoolKind::PathSlope ? kPathSlopeRise : kPathStairsRise;
            const bool foot_here = next->side.opposite() == side;
            return foot_here ? next->surface == square.surface : next->side == side && next->surface + rise == square.surface;
        }

        /// The square at side `side` of (`col`, `row`).
        std::optional<const PoolSquare&> beside(const PoolMap& map, std::int32_t col, std::int32_t row, Side side)
        {
            const auto [dc, dr] = side.step();
            return map.at(col + dc, row + dr);
        }

        /// The surfaces a ramp on the square at (`col`, `row`), against its side `side`, joins: its square's and that
        /// of the square beside it there, as high as the ramp climbs. None where no ramp there meets such a square.
        std::optional<std::pair<std::int32_t, std::int32_t>> ramp_between(const PoolMap& map, std::int32_t col, std::int32_t row, Side side)
        {
            const auto low = map.at(col, row);
            if (!low)
                return std::nullopt;
            const auto [dc, dr] = side.step();
            for (const PoolExtra& e : map.extras)
                if (e.col == col && e.row == row && e.fit.step > 0 && e.side == side)
                    if (const auto high = map.at(col + dc, row + dr, low->surface + e.fit.step); high && high->surface == low->surface + e.fit.step)
                        return std::pair { low->surface, high->surface };
            return std::nullopt;
        }

        /// Takes out the changing rooms that no longer open on a paved square or that stand in the pool.
        void drop_loose_rooms(PoolMap& map)
        {
            std::erase_if(map.rooms, [&](const PoolRoom& r) {
                const auto square = beside(map, r.col, r.row, r.side);
                return map.at(r.col, r.row) || !square || square->kind != PoolKind::Tile;
            });
        }

    } // namespace

    std::optional<const PoolSquare&> PoolMap::at(std::int32_t col, std::int32_t row, std::int32_t level) const noexcept
    {
        std::optional<const PoolSquare&> found;
        for (const PoolSquare& s : squares)
            if (s.col == col && s.row == row && (!found || std::abs(std::int64_t { s.surface } - level) < std::abs(std::int64_t { found->surface } - level)))
                found = s;
        return found;
    }

    std::optional<PoolPathPiece> next_pool_path(const FootpathBuilder& builder)
    {
        const auto spot = next_spot(builder);
        if (!spot)
            return std::nullopt;
        const std::int32_t from = builder.trail.empty() ? builder.start_height.value_or(0) : builder.trail.back().height;
        const PathSlope slope = builder.slope;
        const bool steep = slope == PathSlope::SteepUp || slope == PathSlope::SteepDown;
        const bool up = slope == PathSlope::Up || slope == PathSlope::SteepUp;
        const std::int32_t rise = slope == PathSlope::Flat ? 0 : steep ? kPathStairsRise : kPathSlopeRise;
        const std::int32_t end = up ? from + rise : from - rise;
        const PoolKind kind = rise == 0 ? PoolKind::Path : steep ? PoolKind::PathStairs : PoolKind::PathSlope;
        return PoolPathPiece { .square = { .col = spot->col, .row = spot->row, .kind = kind, .surface = std::min(from, end), .depth = 0, .side = up ? builder.heading.opposite() : builder.heading }, .end = end };
    }

    std::optional<std::int32_t> pool_path_start(const PoolMap& map, const PoolSquare& square, Side side)
    {
        const auto height = side_height(square, side);
        if (!height || met(map, square.col, square.row, side, *height))
            return std::nullopt;
        return height;
    }

    std::optional<std::int32_t> slide_path_start(const PoolMap& map, const SlidePlatform& platform, Side side)
    {
        if (side.on_columns() != platform.turn.facing().on_columns() || met(map, platform.col, platform.row, side, platform.height))
            return std::nullopt;
        return platform.height;
    }

    bool slide_platform_joined(const PoolMap& map, const SlidePlatform& platform, Side side)
    {
        return side.on_columns() == platform.turn.facing().on_columns() && met(map, platform.col, platform.row, side, platform.height);
    }

    std::size_t joined_stations(const PoolMap& map, std::span<const std::optional<SlidePlatform>> platforms)
    {
        std::size_t joined = 0;
        bool counted = false; // Whether the station this piece is on is already counted
        for (const std::optional<SlidePlatform>& p : platforms)
        {
            if (!p)
                counted = false;
            else if (!counted && std::ranges::any_of(kSides, [&](Side side) { return slide_platform_joined(map, *p, side); }))
            {
                ++joined;
                counted = true;
            }
        }
        return joined;
    }

    PoolExtraFit pool_extra_fit(std::string_view words)
    {
        PoolExtraFit fit;
        bool step_next = false;
        for (std::size_t at = 0; at < words.size();)
        {
            const std::size_t start = words.find_first_not_of(' ', at);
            if (start == std::string_view::npos)
                break;
            const std::size_t end = std::min(words.find(' ', start), words.size());
            const std::string_view word = words.substr(start, end - start);
            at = end;
            if (step_next)
            {
                std::from_chars(word.data(), word.data() + word.size(), fit.step);
                step_next = false;
            }
            else if (strings::equals_ignoring_case(word, "LifeGuard"))
                fit.lifeguard = true;
            else if (strings::equals_ignoring_case(word, "SunLounger"))
                fit.lounger = true;
            else if (strings::equals_ignoring_case(word, "Shower"))
                fit.shower = true;
            else if (strings::equals_ignoring_case(word, "DivingBoard"))
                fit.board = true;
            else if (strings::equals_ignoring_case(word, "Ladder"))
                fit.ladder = true;
            else if (strings::equals_ignoring_case(word, "Light"))
                fit.light = true;
            else if (strings::equals_ignoring_case(word, "WaveMachine"))
                fit.wave_machine = true;
            else if (strings::equals_ignoring_case(word, "CentralSolid"))
                fit.paved_middle = true;
            else if (strings::equals_ignoring_case(word, "CentralWater"))
                fit.water_middle = true;
            else if (strings::equals_ignoring_case(word, "Side"))
                fit.side = true;
            else if (strings::equals_ignoring_case(word, "UnderWaterWall"))
                fit.wall = true;
            else if (strings::equals_ignoring_case(word, "OppositeWater"))
                fit.water_across = true;
            else if (strings::equals_ignoring_case(word, "WholeTile"))
                fit.whole = true;
            else if (strings::equals_ignoring_case(word, "HeightStep"))
                step_next = true;
        }
        return fit;
    }

    bool pool_extra_fits(const PoolMap& map, std::int32_t col, std::int32_t row, const PoolExtraFit& fit, Side side)
    {
        const auto square = map.at(col, row);
        if (!square)
            return false;
        const std::uint32_t wants = taken(fit, side);
        std::uint32_t used = 0;
        for (const PoolExtra& e : map.extras)
            if (e.col == col && e.row == row)
                used |= taken(e.fit, e.side);
        return (wants & ~offered(map, *square)) == 0 && (wants & used) == 0;
    }

    std::vector<PoolWallPiece> pool_walls(std::uint8_t water) noexcept
    {
        std::vector<PoolWallPiece> out;
        const auto open = [&](Side side) { return (water & side.bit()) == 0; };
        const std::int32_t sides = static_cast<std::int32_t>(std::popcount(static_cast<std::uint8_t>(~water & 0x0f)));
        if (sides == 4)
            return { { PoolWall::Full, Side::kNextRow } };
        if (sides == 3)
        {
            // The end piece leaves open the side a turn before the one it is turned to (seen by eye), so it
            // is turned one on from the water.
            for (const Side side : kSides)
                if (!open(side))
                    return { { PoolWall::End, side.right() } };
        }
        for (const Side side : kSides)
        {
            if (!open(side))
                continue;
            if (open(side.right()))
                out.push_back({ PoolWall::OuterCorner, side });
            else if (!open(side.left()))
                out.push_back({ PoolWall::Edge, side });
        }
        for (const Side side : kSides)
            if (!open(side) && !open(side.right()) && (water & (kNextBoth << side.value)) == 0)
                out.push_back({ PoolWall::InnerCorner, side });
        return out;
    }

    std::uint8_t water_around(const PoolMap& map, const PoolSquare& square) noexcept
    {
        const auto water = [&](std::int32_t dc, std::int32_t dr) {
            const auto s = map.at(square.col + dc, square.row + dr, square.surface);
            return s && s->kind == PoolKind::Water && s->surface == square.surface;
        };
        std::uint8_t bits = 0;
        for (const Side side : kSides)
        {
            const auto [dc, dr] = side.step();
            const auto [nc, nr] = side.right().step();
            if (water(dc, dr))
                bits |= static_cast<std::uint8_t>(side.bit());
            if (water(dc + nc, dr + nr))
                bits |= static_cast<std::uint8_t>(kNextBoth << side.value);
        }
        return bits;
    }

    bool pool_water_at(const PoolMap& map, const PoolSquare& square, float x, float z)
    {
        constexpr float kCoping = 0.25f;   ///< Of a tile: how far a wall's coping reaches in from its side, as its pieces' models are made
        constexpr float kSweep = 5.0f / 3; ///< In tiles, a loose curve's water's radius, round a point 1.5 tiles out past its open corner, as its pieces' models curve
        const std::uint8_t water = water_around(map, square);
        const auto walled = [&](Side side) { return (water & side.bit()) == 0; };
        // How far in from side `side`, in tiles.
        const auto in = [&](Side side) {
            const auto [dc, dr] = side.step();
            return 0.5f - (static_cast<float>(dc) * x + static_cast<float>(dr) * z);
        };
        for (const Side side : kSides)
            if (walled(side) && in(side) < kCoping)
                return false;
        const bool sweeping = sweeps(map, square);
        for (const Side side : kSides)
        {
            const float a = in(side);
            const float b = in(side.right());
            if (!walled(side) && !walled(side.right()) && (water & (kNextBoth << side.value)) == 0 && std::hypot(a, b) < kCoping)
                return false;
            if (!walled(side) || !walled(side.right()) || sweeping || square.edge == PoolEdge::Straight)
                continue;
            const bool two_walls = !walled(side.opposite()) && !walled(side.left());
            if (square.edge == PoolEdge::Diagonal)
            {
                // Cut across from where one coping meets the open side to where the other does.
                if (two_walls && a + b < 1 + kCoping)
                    return false;
                continue;
            }
            // Rounded as wide as the water reaches past the corner; halfway across where a strip's walls face each
            // other, so a strip's end rounds in a half circle and a lone square in a whole one.
            const auto reach = [&](Side opposite) { return walled(opposite) ? (1 - 2 * kCoping) / 2 : 1 - kCoping; };
            const float r = std::min(reach(side.opposite()), reach(side.left()));
            const float centre = kCoping + r;
            if (a < centre && b < centre && std::hypot(centre - a, centre - b) > r)
                return false;
        }
        // A loose curve's water, in its corner square and the squares either side.
        for (std::int32_t k = -1; k < 4; ++k)
        {
            const auto corner = k < 0 ? std::optional<const PoolSquare&>(square) : water_beside(map, square, kSides[static_cast<std::size_t>(k)]);
            if (!sweeps(map, corner))
                continue;
            const auto walls = pool_walls(water_around(map, *corner));
            const auto [ac, ar] = walls[0].side.step();
            const auto [bc, br] = walls[0].side.right().step();
            const float cx = static_cast<float>(corner->col - square.col) - 1.5f * static_cast<float>(ac + bc);
            const float cz = static_cast<float>(corner->row - square.row) - 1.5f * static_cast<float>(ar + br);
            if (std::hypot(x - cx, z - cz) > kSweep)
                return false;
        }
        return true;
    }

    std::vector<PoolPiece> pool_pieces(const PoolMap& map, const PoolSquare& square, std::span<const PoolOpening> openings)
    {
        std::vector<PoolPiece> out;
        if (square.kind == PoolKind::PathSlope || square.kind == PoolKind::PathStairs)
            return { { square.kind == PoolKind::PathSlope ? "PathSlope" : "PathStairs", square.surface, ModelTurn::toward(square.side) } };
        if (square.kind == PoolKind::Path)
        {
            // One piece, its shape from the sides where something joins it, turned as saved parks show.
            std::uint8_t joins = 0;
            for (const Side side : kSides)
                if (path_joins(map, square, side, openings))
                    joins |= static_cast<std::uint8_t>(side.bit());
            const auto count = std::popcount(joins);
            const auto has = [&](Side side) { return (joins & side.bit()) != 0; };
            if (count == 4)
                return { { "PathXRoads", square.surface, { } } };
            for (const Side side : kSides)
            {
                // Turned so its closed side, its model's side 1, is the side nothing joins.
                if (count == 3 && !has(side))
                    return { { "PathTJunction", square.surface, ModelTurn::toward(side.left()) } };
                if (count == 2 && has(side) && has(side.right()))
                    return { { "PathCorner", square.surface, ModelTurn::toward(side) } };
            }
            // Straight, along the sides joined, or with none the way it was laid: a trail's first piece joins only
            // the pool's paving or a slide's platform, and runs on from it.
            constexpr ModelTurn kAlongRows { 2 };    ///< The straight piece running from row to row, as saved parks show
            constexpr ModelTurn kAlongColumns { 3 }; ///< Running from column to column
            const bool across = count > 0 ? has(Side::kNextCol) || has(Side::kColBefore) : square.side.on_columns();
            return { { "PathStraight", square.surface, across ? kAlongColumns : kAlongRows } };
        }
        const std::int32_t floor = square.surface - square.depth;
        const auto stack = [&](const std::array<std::string_view, 3>& names, ModelTurn turn) {
            for (std::int32_t h = floor; h <= square.surface; ++h)
                out.push_back({ names[h == floor ? 0 : h == square.surface ? 2 : 1], h, turn });
        };
        if (square.kind == PoolKind::Water)
        {
            out.push_back({ "Base", floor, { } });
            for (const PoolWallPiece& wall : pool_walls(water_around(map, square)))
            {
                const auto [names, side] = wall_stack(map, square, wall);
                stack(names, ModelTurn::toward(side));
            }
        }
        else
        {
            stack({ "SolidBot", "SolidMid", "SolidTop" }, { });
            if (square.kind == PoolKind::Jacuzzi)
                out.back().symbol = "Jacuzzi";
        }
        if (square.kind == PoolKind::Stairs)
        {
            // Its foot on the surface in place of the paving, each unit up a quarter turn back round, its top
            // a unit under the level it climbs to.
            const std::int32_t climb = std::max(square.board, kLowestStairs);
            const ModelTurn foot = ModelTurn::toward(square.side);
            out.back() = { "StairsBase", square.surface, foot };
            for (std::int32_t k = 1; k + 1 < climb; ++k)
                out.push_back({ "StairsMid", square.surface + k, foot.turned(-k) });
            out.push_back({ "StairsTop", square.surface + climb - 1, foot.turned(1 - climb) });
        }
        if (square.kind == PoolKind::HighDive)
        {
            // Its foot on the surface, a stretch of ladder and a board for each level but the last, and the top
            // board; turned, as saved parks show, a half turn from the side it faces.
            const ModelTurn turn = ModelTurn::toward(square.side.opposite());
            const std::int32_t levels = std::max(square.board / kBoardLevel, 1);
            out.push_back({ "DBBase", square.surface, turn });
            for (std::int32_t level = 0; level + 1 < levels; ++level)
            {
                out.push_back({ "DBMidBoard", square.surface + 2 + kBoardLevel * level, turn });
                out.push_back({ "DBMid", square.surface + 3 + kBoardLevel * level, turn });
            }
            out.push_back({ "DBTopBoard", square.surface + 2 + kBoardLevel * (levels - 1), turn });
        }
        for (const Side side : kSides)
        {
            const auto [dc, dr] = side.step();
            const bool room = std::ranges::any_of(map.rooms, [&](const PoolRoom& r) { return r.col == square.col + dc && r.row == square.row + dr && r.side.opposite() == side; });
            const auto next = map.at(square.col + dc, square.row + dr, square.surface);
            // Open where a way up or down meets it: a ramp on it climbing at that side, a ramp beside it climbing up
            // to it, or the top of a staircase beside it.
            const auto own_ramp = ramp_between(map, square.col, square.row, side);
            const auto ramp_up = ramp_between(map, square.col + dc, square.row + dr, side.opposite());
            const bool stairs_up = std::ranges::any_of(map.squares, [&](const PoolSquare& s) { return s.kind == PoolKind::Stairs && s.col == square.col + dc && s.row == square.row + dr && s.surface + std::max(s.board, kLowestStairs) == square.surface; });
            const bool way = (own_ramp && own_ramp->first == square.surface) || (ramp_up && ramp_up->second == square.surface) || stairs_up;
            if (!room && !way && !opened(openings, square, side) && (!next || next->surface != square.surface))
                out.push_back({ "Railing", square.surface, ModelTurn::toward(side) });
            // A staircase's top is railed where no square meets it.
            const std::int32_t top = square.surface + std::max(square.board, kLowestStairs);
            if (const auto above = map.at(square.col + dc, square.row + dr, top); square.kind == PoolKind::Stairs && (!above || above->surface != top))
                out.push_back({ "Railing", top, ModelTurn::toward(side) });
        }
        return out;
    }

    std::vector<save::PlacedObject> pool_objects(const PoolMap& map, std::string_view overlay, std::uint64_t last_id, std::span<const PoolOpening> openings)
    {
        std::vector<save::PlacedObject> out;
        for (const PoolSquare& square : map.squares)
            for (const PoolPiece& piece : pool_pieces(map, square, openings))
            {
                save::PlacedObject& o = out.emplace_back();
                o.id = ++last_id;
                o.owner_ref = o.id;
                o.fixture = true;
                o.symbol = piece.symbol;
                o.overlay = overlay;
                o.col = square.col;
                o.row = square.row;
                // A high diving board's boards cover three squares in a row along their own Z, the tower's in the
                // middle, and are named by the first of them, as saved parks hold them.
                if (piece.symbol == "DBMidBoard" || piece.symbol == "DBTopBoard")
                    (piece.turn.facing().on_columns() ? o.col : o.row) -= 1;
                o.base_height = piece.height;
                o.force_absolute_height = true;
                o.rotation = piece.turn.value;
            }
        for (const PoolExtra& e : map.extras)
        {
            const auto square = map.at(e.col, e.row);
            save::PlacedObject& o = out.emplace_back();
            o.id = ++last_id;
            o.owner_ref = o.id;
            o.fixture = true;
            o.symbol = e.symbol;
            o.overlay = e.overlay;
            o.col = e.col;
            o.row = e.row;
            o.base_height = square ? square->surface : 0;
            o.force_absolute_height = true;
            o.rotation = ModelTurn::toward(e.side).value;
        }
        for (const PoolRoom& r : map.rooms)
        {
            const auto square = beside(map, r.col, r.row, r.side);
            save::PlacedObject& o = out.emplace_back();
            o.id = ++last_id;
            o.owner_ref = o.id;
            o.fixture = true;
            o.symbol = r.symbol;
            o.overlay = r.overlay;
            o.col = r.col;
            o.row = r.row;
            o.base_height = square ? square->surface : 0;
            o.force_absolute_height = true;
            // Its door is a turn on from the side it opens on, as parks saved with changing rooms show.
            o.rotation = ModelTurn::toward(r.side.right()).value;
        }
        return out;
    }

    std::vector<save::PoolWater> pool_water(const PoolMap& map)
    {
        std::vector<save::PoolWater> out;
        for (const PoolSquare& s : map.squares)
            if (s.kind == PoolKind::Water)
                out.push_back({ s.col, s.row, s.surface, s.depth });
        return out;
    }

    void drop_saved_pool_pieces(const PoolMap& map, std::string_view overlay, std::vector<save::PlacedObject>& objects)
    {
        std::set<std::string, std::less<>> laid;
        for (const save::PlacedObject& o : pool_objects(map, overlay, 0))
            laid.insert(o.symbol);
        const auto same_overlay = [&](std::string_view o) { return std::ranges::equal(o, overlay, [](char a, char b) { return strings::ascii_lower(a == '\\' ? '/' : a) == strings::ascii_lower(b == '\\' ? '/' : b); }); };
        std::erase_if(objects, [&](const save::PlacedObject& o) { return same_overlay(o.overlay) && laid.contains(o.symbol); });
    }

    void dig_under_pools(PoolMap& map, save::TerrainGrid& grid)
    {
        for (PoolSquare& s : map.squares)
            if (grid.contains(s.col, s.row))
            {
                auto& corners = grid.at(s.col, s.row).corners;
                const auto floor = static_cast<float>(s.surface - s.depth);
                if (std::ranges::any_of(corners, [&](float h) { return h > floor; }))
                    s.ground = s.ground.value_or(corners);
                for (float& h : corners)
                    h = std::min(h, floor);
            }
    }

    std::int64_t pool_build_cost(std::span<const std::int32_t, 18> prices, const PoolSquare& square) noexcept
    {
        return price(prices, square, kBuild);
    }

    std::int64_t pool_refund(std::span<const std::int32_t, 18> prices, const PoolSquare& square) noexcept
    {
        return price(prices, square, kRefund);
    }

    bool pool_room_fits(const PoolMap& map, std::int32_t col, std::int32_t row, Side side)
    {
        const auto square = beside(map, col, row, side);
        const auto [dc, dr] = side.step();
        const bool taken = std::ranges::any_of(map.rooms, [&](const PoolRoom& r) {
            const auto [rc, rr] = r.side.step();
            return (r.col == col && r.row == row) || (r.col + rc == col + dc && r.row + rr == row + dr);
        });
        return !map.at(col, row) && square && square->kind == PoolKind::Tile && !taken;
    }

    std::vector<save::Tile> pool_complex(const PoolMap& map, std::size_t room)
    {
        std::vector<save::Tile> complex;
        if (room >= map.rooms.size())
            return { };
        if (const auto first = beside(map, map.rooms[room].col, map.rooms[room].row, map.rooms[room].side))
            complex.push_back({ first->col, first->row });
        for (std::size_t i = 0; i < complex.size(); ++i)
            for (const Side side : kSides)
                if (const auto next = beside(map, complex[i].col, complex[i].row, side); next && !std::ranges::contains(complex, save::Tile { next->col, next->row }))
                    complex.push_back({ next->col, next->row });
        return complex;
    }

    std::vector<std::size_t> pool_complex_rooms(const PoolMap& map, std::size_t room)
    {
        const std::vector<save::Tile> complex = pool_complex(map, room);
        std::vector<std::size_t> out;
        for (std::size_t i = 0; i < map.rooms.size(); ++i)
            if (const auto square = beside(map, map.rooms[i].col, map.rooms[i].row, map.rooms[i].side); square && std::ranges::contains(complex, save::Tile { square->col, square->row }))
                out.push_back(i);
        return out;
    }

    std::int64_t lay_pool(PoolMap& map, std::span<const std::int32_t, 18> prices, std::span<const PoolSquare> squares)
    {
        std::int64_t cost = 0;
        for (const PoolSquare& square : squares)
        {
            const auto in_the_way = [&](const PoolSquare& s) { return s.col == square.col && s.row == square.row && !stacked(s, square); };
            const auto was = std::ranges::find_if(map.squares, in_the_way);
            if (was == map.squares.end())
            {
                cost += pool_build_cost(prices, square);
                map.squares.push_back(square);
            }
            else if (!same(*was, square))
            {
                // It takes the place of every square in its way, and keeps the ground the first had dug.
                cost += pool_build_cost(prices, square) + pool_refund(prices, *was);
                const auto ground = was->ground;
                *was = square;
                was->ground = ground;
                const std::size_t kept = static_cast<std::size_t>(was - map.squares.begin());
                for (std::size_t i = map.squares.size(); i-- > 0;)
                    if (i != kept && in_the_way(map.squares[i]))
                    {
                        cost += pool_refund(prices, map.squares[i]);
                        map.squares.erase(map.squares.begin() + static_cast<std::ptrdiff_t>(i));
                    }
                cost += clear_extras(map, square.col, square.row);
            }
            else
                was->edge = square.edge; // A new edge setting alone costs nothing
        }
        drop_loose_rooms(map);
        return cost;
    }

    std::int64_t take_up_pool(PoolMap& map, std::span<const std::int32_t, 18> prices, save::Tile a, save::Tile b, std::optional<std::int32_t> level)
    {
        const auto [c0, c1] = std::minmax(a.col, b.col);
        const auto [r0, r1] = std::minmax(a.row, b.row);
        std::int64_t cost = 0;
        // Each square going, marked first: which of a stack is nearest the level is seen before any goes.
        std::vector<bool> going(map.squares.size());
        for (std::size_t i = 0; i < map.squares.size(); ++i)
        {
            const PoolSquare& s = map.squares[i];
            going[i] = s.col >= c0 && s.col <= c1 && s.row >= r0 && s.row <= r1 && (!level || &*map.at(s.col, s.row, *level) == &s);
        }
        for (std::size_t i = map.squares.size(); i-- > 0;)
            if (going[i])
            {
                cost += pool_refund(prices, map.squares[i]);
                map.squares.erase(map.squares.begin() + static_cast<std::ptrdiff_t>(i));
            }
        std::erase_if(map.extras, [&](const PoolExtra& e) {
            if (e.col < c0 || e.col > c1 || e.row < r0 || e.row > r1)
                return false;
            cost += e.refund;
            return true;
        });
        drop_loose_rooms(map);
        return cost;
    }

    PoolMap saved_pools(const save::WorldModel& world, std::span<const save::PlacedObject> objects)
    {
        constexpr std::int32_t kWater = 0, kStairs = 2, kHighDive = 3, kSpa = 4, kPath = 5, kPathSlope = 6, kPathStairs = 7; // Of the stored types
        PoolMap map;
        for (const save::PoolSegmentState& s : world.pool_segments)
        {
            PoolSquare& square = map.squares.emplace_back(PoolSquare { .col = s.col, .row = s.row, .surface = s.surface, .depth = s.depth });
            if (s.ground && std::ranges::any_of(*s.ground, [&](float h) { return h > static_cast<float>(s.surface - s.depth); }))
                square.ground = s.ground;
            square.kind = s.type == kWater ? PoolKind::Water : s.type == kSpa ? PoolKind::Jacuzzi : s.type == kHighDive ? PoolKind::HighDive : s.type == kStairs ? PoolKind::Stairs : s.type == kPath ? PoolKind::Path : s.type == kPathSlope ? PoolKind::PathSlope : s.type == kPathStairs ? PoolKind::PathStairs : PoolKind::Tile;
            square.board = s.type == kHighDive || s.type == kStairs ? s.special : 0;
            constexpr std::array kEdges { PoolEdge::Straight, PoolEdge::Tight, PoolEdge::Diagonal, PoolEdge::Loose }; // By the stored setting
            square.edge = kEdges[static_cast<std::size_t>(s.flags & 3)];
        }
        // A board faces the side it keeps, which may look down past water at its own level to a pool below; one kept
        // without it faces the water beside it. A spiral staircase has its foot at the side it keeps. A path's slope or stairs has its foot at the path beside it level
        // with it, or at the slope or stairs below whose top it carries on from.
        constexpr std::array kStoredSides { Side::kColBefore, Side::kNextRow, Side::kNextCol, Side::kRowBefore }; // By bit of the stored side
        for (std::size_t i = 0; i < map.squares.size(); ++i)
        {
            PoolSquare& square = map.squares[i];
            const auto side = static_cast<std::uint32_t>(world.pool_segments[i].side);
            if ((square.kind == PoolKind::HighDive || square.kind == PoolKind::Stairs) && std::has_single_bit(side) && side < 16)
            {
                square.side = kStoredSides[static_cast<std::size_t>(std::countr_zero(side))];
                continue;
            }
            const bool climbs = square.kind == PoolKind::PathSlope || square.kind == PoolKind::PathStairs;
            for (std::size_t k = 0; (square.kind == PoolKind::HighDive || climbs) && k < kSides.size(); ++k)
                if (const auto next = map.at(square.col + kSides[k].step()[0], square.row + kSides[k].step()[1], square.surface); next && (climbs ? (next->kind == PoolKind::Path && next->surface == square.surface) || (next->kind == PoolKind::PathSlope && next->surface + kPathSlopeRise == square.surface) || (next->kind == PoolKind::PathStairs && next->surface + kPathStairsRise == square.surface) : next->kind == PoolKind::Water))
                {
                    square.side = kSides[k];
                    break;
                }
        }
        for (const save::ChangingRoomState& room : world.changing_rooms)
        {
            const auto object = std::ranges::find(objects, room.object_ref, &save::PlacedObject::id);
            const auto segment = std::ranges::find(world.pool_segments, room.segment, &save::PoolSegmentState::id);
            if (object == objects.end() || segment == world.pool_segments.end())
                continue;
            const auto side = Side::towards(segment->col - object->col, segment->row - object->row);
            if (!side)
                continue;
            const auto complex = std::ranges::find(world.pool_complexes, segment->complex, &save::PoolComplexState::index);
            const bool named = complex != world.pool_complexes.end() && !complex->name.starts_with("%ME%");
            map.rooms.push_back({ .symbol = object->symbol, .overlay = object->overlay, .col = object->col, .row = object->row, .side = *side, .ticket = complex != world.pool_complexes.end() ? complex->ticket : kPoolTicket, .open = room.open, .complex_name = named ? complex->name : std::string(), .fluid = static_cast<std::uint8_t>(std::clamp(room.fluid, 0, 2)) });
            if (complex != world.pool_complexes.end())
                map.rooms.back().waves = { .height = complex->wave_height, .frequency = complex->wave_frequency, .on = complex->wave_on, .every = complex->wave_every };
        }
        return map;
    }

} // namespace rct3::park
