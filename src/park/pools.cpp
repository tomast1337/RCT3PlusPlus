// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/pools.hpp"

#include "strings/ascii.hpp"

#include <algorithm>
#include <bit>
#include <charconv>

namespace rct3::park
{

    namespace
    {

        constexpr std::array<std::array<std::int32_t, 2>, 4> kSideStep { { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } } }; // By turn

        /// A wall of the pool type, the names of its pieces from the bottom of a stack up.
        using WallStack = std::array<std::string_view, 3>;
        constexpr WallStack kEdge { "EdgeBot", "EdgeMid", "EdgeTop" };
        constexpr WallStack kSquareCorner { "Corner1Bot", "Corner1Mid", "Corner1Top" };
        constexpr WallStack kCutCorner { "Corner2Bot", "Corner2Mid", "Corner2Top" };
        constexpr WallStack kSquareInside { "Corner3Bot", "Corner3Mid", "Corner3Top" };
        constexpr WallStack kEnd { "EndBot", "EndMid", "EndTop" };
        constexpr WallStack kFull { "FullBot", "FullMid", "FullTop" };
        constexpr WallStack kCurvedEnd { "CurveEndBot", "CurveEndMid", "CurveEndTop" };
        constexpr WallStack kCurvedFull { "CurveFullBot", "CurveFullMid", "CurveFullTop" };
        constexpr WallStack kSweep { "CurveCorner9Bot", "CurveCorner9Mid", "CurveCorner9Top" };
        /// An edge meeting a sweep: at its next turn, at its turn before, at both.
        constexpr std::array<WallStack, 3> kSweptEdge { {
            { "CurveEdge1Bot", "CurveEdge1Mid", "CurveEdge1Top" },
            { "CurveEdge2Bot", "CurveEdge2Mid", "CurveEdge2Top" },
            { "CurveEdge3Bot", "CurveEdge3Mid", "CurveEdge3Top" },
        } };
        /// A tight curve, alone, then meeting a sweep at its turn before, at its turn after next, at both.
        constexpr std::array<WallStack, 4> kTightCorner { {
            { "CurveCorner5Bot", "CurveCorner5Mid", "CurveCorner5Top" },
            { "CurveCorner7Bot", "CurveCorner7Mid", "CurveCorner7Top" },
            { "CurveCorner6Bot", "CurveCorner6Mid", "CurveCorner6Top" },
            { "CurveCorner8Bot", "CurveCorner8Mid", "CurveCorner8Top" },
        } };
        /// A rounded inside corner, alone, then meeting a sweep at its turn, at its turn before, at both.
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
            return a.kind == b.kind && a.depth == b.depth && a.surface == b.surface && a.board == b.board && a.turn == b.turn;
        }

        /// The parts of a square, as bits: its middle, paved or water, then four bits each, by turn, for its
        /// sides, the walls under them, water at them and changes of level at them.
        constexpr std::uint32_t kPavedMiddle = 1u << 0;
        constexpr std::uint32_t kWaterMiddle = 1u << 1;
        constexpr std::uint32_t kSides = 2; // The first bit of each four
        constexpr std::uint32_t kWalls = 6;
        constexpr std::uint32_t kWaterAt = 10;
        constexpr std::uint32_t kStepAt = 14;

        std::uint32_t at_side(std::uint32_t first, std::int32_t turn) noexcept
        {
            return 1u << (first + (turn & 3));
        }

        /// The parts an extra that fits as `fit`, against side `turn`, takes.
        std::uint32_t taken(const PoolExtraFit& fit, std::int32_t turn) noexcept
        {
            std::uint32_t parts = (fit.paved_middle ? kPavedMiddle : 0u) | (fit.water_middle ? kWaterMiddle : 0u);
            if (fit.side)
                parts |= at_side(kSides, turn) | (fit.step > 0 ? at_side(kStepAt, turn) : 0u);
            if (fit.wall)
                parts |= at_side(kWalls, turn);
            if (fit.water_across)
                parts |= at_side(kWaterAt, turn + 2);
            if (fit.whole)
                parts |= kPavedMiddle | (0xfu << kSides);
            return parts;
        }

        /// The parts `square` offers extras.
        std::uint32_t offered(const PoolMap& map, const PoolSquare& square) noexcept
        {
            if (square.kind == PoolKind::Tile)
                // shortcut: a paved square offers no change of level; give it the sides that meet a square
                // a level up or down once pools have levels.
                return kPavedMiddle | (0xfu << kSides);
            if (square.kind != PoolKind::Water)
                return 0;
            const std::uint8_t water = water_around(map, square);
            std::uint32_t parts = kWaterMiddle;
            for (std::int32_t turn = 0; turn < 4; ++turn)
                parts |= (water & (1u << turn)) != 0 ? at_side(kWaterAt, turn) : at_side(kSides, turn) | at_side(kWalls, turn) | at_side(kStepAt, turn);
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

        /// The water square at side `turn` of `square`, at its surface.
        std::optional<const PoolSquare&> water_beside(const PoolMap& map, const PoolSquare& square, std::int32_t turn)
        {
            const auto [dc, dr] = kSideStep[static_cast<std::size_t>(turn & 3)];
            const auto s = map.at(square.col + dc, square.row + dr);
            if (!s || s->kind != PoolKind::Water || s->surface != square.surface)
                return std::nullopt;
            return s;
        }

        bool has_wall(const std::vector<PoolWallPiece>& walls, PoolWall shape)
        {
            return std::ranges::contains(walls, shape, &PoolWallPiece::shape);
        }

        /// Whether `square` is the corner of a loose curve that sweeps on into the squares beside it: none of
        /// them walled straight or cut across, a lone square or the end of a strip.
        bool sweeps(const PoolMap& map, std::optional<const PoolSquare&> square)
        {
            if (!square || square->edge != PoolEdge::Loose || !has_wall(pool_walls(water_around(map, *square)), PoolWall::OuterCorner))
                return false;
            for (std::int32_t turn = 0; turn < 4; ++turn)
                if (const auto next = water_beside(map, *square, turn))
                {
                    const auto walls = pool_walls(water_around(map, *next));
                    const bool angular = next->edge == PoolEdge::Straight || next->edge == PoolEdge::Diagonal;
                    if (has_wall(walls, PoolWall::End) || has_wall(walls, PoolWall::Full) || (angular && (has_wall(walls, PoolWall::Edge) || has_wall(walls, PoolWall::OuterCorner))))
                        return false;
                }
            return true;
        }

        /// Whether `square` has a square outside corner.
        bool square_cornered(const PoolMap& map, std::optional<const PoolSquare&> square)
        {
            return square && square->edge == PoolEdge::Straight && has_wall(pool_walls(water_around(map, *square)), PoolWall::OuterCorner);
        }

        /// The wall stack and its turn for `wall` of the water square `square`.
        std::pair<WallStack, std::int32_t> wall_stack(const PoolMap& map, const PoolSquare& square, const PoolWallPiece& wall)
        {
            const std::int32_t t = wall.turn;
            const auto swept = [&](std::int32_t turn) { return sweeps(map, water_beside(map, square, turn)); };
            const bool curved = square.edge == PoolEdge::Tight || square.edge == PoolEdge::Loose;
            // Which of two sides meet a sweep, as an index: none 0, the first 1, the second 2, both 3.
            const auto meeting = [&](std::int32_t a, std::int32_t b) { return static_cast<std::size_t>((swept(a) ? 1 : 0) + (swept(b) ? 2 : 0)); };
            switch (wall.shape)
            {
                case PoolWall::Edge:
                {
                    const std::size_t at = meeting(t + 1, t - 1);
                    return { at == 0 ? kEdge : kSweptEdge[at - 1], t };
                }
                case PoolWall::OuterCorner:
                    if (sweeps(map, square))
                        return { kSweep, t };
                    if (square.edge == PoolEdge::Straight)
                        return { kSquareCorner, t };
                    if (square.edge == PoolEdge::Diagonal)
                        return { kCutCorner, t };
                    return { kTightCorner[meeting(t - 1, t + 2)], t };
                case PoolWall::InnerCorner:
                {
                    // Turned to the second of the two sides whose water meets round it.
                    const std::int32_t turn = (t + 1) & 3;
                    if (const std::size_t at = meeting(t + 1, t); at != 0)
                        return { kInside[at], turn };
                    const bool square_meets = square_cornered(map, square) || square_cornered(map, water_beside(map, square, t)) || square_cornered(map, water_beside(map, square, t + 1));
                    return { square_meets ? kSquareInside : kInside[0], turn };
                }
                case PoolWall::End:
                    return { curved ? kCurvedEnd : kEnd, t };
                case PoolWall::Full:
                    return { curved ? kCurvedFull : kFull, t };
            }
            return { kEdge, t };
        }

        /// Whether a path joins `square`, a path, at its side `turn`: a path at its surface, or the foot or the
        /// top of a slope or of stairs facing it at its surface.
        bool path_joins(const PoolMap& map, const PoolSquare& square, std::int32_t turn)
        {
            const auto [dc, dr] = kSideStep[static_cast<std::size_t>(turn & 3)];
            const auto next = map.at(square.col + dc, square.row + dr);
            if (!next || !is_pool_path(next->kind))
                return false;
            if (next->kind == PoolKind::Path)
                return next->surface == square.surface;
            const std::int32_t rise = next->kind == PoolKind::PathSlope ? kPathSlopeRise : kPathStairsRise;
            const bool foot_here = ((next->turn + 2) & 3) == (turn & 3);
            return foot_here ? next->surface == square.surface : (next->turn & 3) == (turn & 3) && next->surface + rise == square.surface;
        }

        /// The square at side `turn` of (`col`, `row`).
        std::optional<const PoolSquare&> beside(const PoolMap& map, std::int32_t col, std::int32_t row, std::int32_t turn)
        {
            const auto [dc, dr] = kSideStep[static_cast<std::size_t>(turn & 3)];
            return map.at(col + dc, row + dr);
        }

        /// Takes out the changing rooms that no longer open on a paved square or that stand in the pool.
        void drop_loose_rooms(PoolMap& map)
        {
            std::erase_if(map.rooms, [&](const PoolRoom& r) {
                const auto square = beside(map, r.col, r.row, r.turn);
                return map.at(r.col, r.row) || !square || square->kind != PoolKind::Tile;
            });
        }

    } // namespace

    std::optional<const PoolSquare&> PoolMap::at(std::int32_t col, std::int32_t row) const noexcept
    {
        const auto found = std::ranges::find_if(squares, [&](const PoolSquare& s) { return s.col == col && s.row == row; });
        if (found == squares.end())
            return std::nullopt;
        return *found;
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

    bool pool_extra_fits(const PoolMap& map, std::int32_t col, std::int32_t row, const PoolExtraFit& fit, std::int32_t turn)
    {
        const auto square = map.at(col, row);
        if (!square)
            return false;
        const std::uint32_t wants = taken(fit, turn);
        std::uint32_t used = 0;
        for (const PoolExtra& e : map.extras)
            if (e.col == col && e.row == row)
                used |= taken(e.fit, e.turn);
        return (wants & ~offered(map, *square)) == 0 && (wants & used) == 0;
    }

    std::vector<PoolWallPiece> pool_walls(std::uint8_t water) noexcept
    {
        std::vector<PoolWallPiece> out;
        const auto open = [&](std::int32_t turn) { return (water & (1u << (turn & 3))) == 0; };
        const std::int32_t sides = static_cast<std::int32_t>(std::popcount(static_cast<std::uint8_t>(~water & 0x0f)));
        if (sides == 4)
            return { { PoolWall::Full, 0 } };
        if (sides == 3)
        {
            // The end piece leaves open the side a turn before the one it is turned to (seen by eye), so it
            // is turned one on from the water.
            for (std::int32_t turn = 0; turn < 4; ++turn)
                if (!open(turn))
                    return { { PoolWall::End, (turn + 1) & 3 } };
        }
        for (std::int32_t turn = 0; turn < 4; ++turn)
        {
            if (!open(turn))
                continue;
            if (open(turn + 1))
                out.push_back({ PoolWall::OuterCorner, turn });
            else if (!open(turn - 1))
                out.push_back({ PoolWall::Edge, turn });
        }
        for (std::int32_t turn = 0; turn < 4; ++turn)
            if (!open(turn) && !open(turn + 1) && (water & (kNextBoth << turn)) == 0)
                out.push_back({ PoolWall::InnerCorner, turn });
        return out;
    }

    std::uint8_t water_around(const PoolMap& map, const PoolSquare& square) noexcept
    {
        const auto water = [&](std::int32_t dc, std::int32_t dr) {
            const auto s = map.at(square.col + dc, square.row + dr);
            return s && s->kind == PoolKind::Water && s->surface == square.surface;
        };
        std::uint8_t bits = 0;
        for (std::int32_t turn = 0; turn < 4; ++turn)
        {
            const auto [dc, dr] = kSideStep[static_cast<std::size_t>(turn)];
            const auto [nc, nr] = kSideStep[static_cast<std::size_t>((turn + 1) & 3)];
            if (water(dc, dr))
                bits |= static_cast<std::uint8_t>(1u << turn);
            if (water(dc + nc, dr + nr))
                bits |= static_cast<std::uint8_t>(kNextBoth << turn);
        }
        return bits;
    }

    std::vector<PoolPiece> pool_pieces(const PoolMap& map, const PoolSquare& square)
    {
        std::vector<PoolPiece> out;
        if (square.kind == PoolKind::PathSlope || square.kind == PoolKind::PathStairs)
            return { { square.kind == PoolKind::PathSlope ? "PathSlope" : "PathStairs", square.surface, square.turn & 3 } };
        if (square.kind == PoolKind::Path)
        {
            // One piece, its shape from the sides where a path joins it, turned as saved parks show.
            std::uint8_t joins = 0;
            for (std::int32_t turn = 0; turn < 4; ++turn)
                if (path_joins(map, square, turn))
                    joins |= static_cast<std::uint8_t>(1u << turn);
            const auto count = std::popcount(joins);
            const auto has = [&](std::int32_t turn) { return (joins & (1u << (turn & 3))) != 0; };
            if (count == 4)
                return { { "PathXRoads", square.surface, 0 } };
            for (std::int32_t turn = 0; turn < 4; ++turn)
            {
                // shortcut: no saved park has a junction, so its turn is a guess; check it once one does.
                if (count == 3 && !has(turn))
                    return { { "PathTJunction", square.surface, (turn + 1) & 3 } };
                if (count == 2 && has(turn) && has(turn + 1))
                    return { { "PathCorner", square.surface, turn } };
            }
            // Straight, along the sides joined, or along the rows with none.
            const bool across = count > 0 && (has(1) || has(3));
            return { { "PathStraight", square.surface, across ? 3 : 2 } };
        }
        const std::int32_t floor = square.surface - square.depth;
        const auto stack = [&](const std::array<std::string_view, 3>& names, std::int32_t turn) {
            for (std::int32_t h = floor; h <= square.surface; ++h)
                out.push_back({ names[h == floor ? 0 : h == square.surface ? 2 : 1], h, turn });
        };
        if (square.kind == PoolKind::Water)
        {
            out.push_back({ "Base", floor, 0 });
            for (const PoolWallPiece& wall : pool_walls(water_around(map, square)))
            {
                const auto [names, turn] = wall_stack(map, square, wall);
                stack(names, turn);
            }
        }
        else
        {
            stack({ "SolidBot", "SolidMid", "SolidTop" }, 0);
            if (square.kind == PoolKind::Jacuzzi)
                out.back().symbol = "Jacuzzi";
        }
        if (square.kind == PoolKind::Stairs)
        {
            // Its foot on the surface in place of the paving, each unit up a quarter turn back round, its top
            // a unit under the level it climbs to.
            const std::int32_t climb = std::max(square.board, kLowestStairs);
            out.back() = { "StairsBase", square.surface, square.turn & 3 };
            for (std::int32_t k = 1; k + 1 < climb; ++k)
                out.push_back({ "StairsMid", square.surface + k, (square.turn - k) & 3 });
            out.push_back({ "StairsTop", square.surface + climb - 1, (square.turn - climb + 1) & 3 });
        }
        if (square.kind == PoolKind::HighDive)
        {
            // Its foot on the surface, a stretch of ladder and a board for each level but the last, and the top
            // board; turned, as saved parks show, a half turn from the side it faces.
            const std::int32_t turn = (square.turn + 2) & 3;
            const std::int32_t levels = std::max(square.board / kBoardLevel, 1);
            out.push_back({ "DBBase", square.surface, turn });
            for (std::int32_t level = 0; level + 1 < levels; ++level)
            {
                out.push_back({ "DBMidBoard", square.surface + 2 + kBoardLevel * level, turn });
                out.push_back({ "DBMid", square.surface + 3 + kBoardLevel * level, turn });
            }
            out.push_back({ "DBTopBoard", square.surface + 2 + kBoardLevel * (levels - 1), turn });
        }
        for (std::int32_t turn = 0; turn < 4; ++turn)
        {
            const auto [dc, dr] = kSideStep[static_cast<std::size_t>(turn)];
            const bool room = std::ranges::any_of(map.rooms, [&](const PoolRoom& r) { return r.col == square.col + dc && r.row == square.row + dr && ((r.turn + 2) & 3) == turn; });
            const auto next = map.at(square.col + dc, square.row + dr);
            if (!room && (!next || next->surface != square.surface))
                out.push_back({ "Railing", square.surface, turn });
            // A staircase's top is railed where no square meets it.
            if (const std::int32_t top = square.surface + std::max(square.board, kLowestStairs); square.kind == PoolKind::Stairs && (!next || next->surface != top))
                out.push_back({ "Railing", top, turn });
        }
        return out;
    }

    std::vector<save::PlacedObject> pool_objects(const PoolMap& map, std::string_view overlay, std::uint64_t last_id)
    {
        std::vector<save::PlacedObject> out;
        for (const PoolSquare& square : map.squares)
            for (const PoolPiece& piece : pool_pieces(map, square))
            {
                save::PlacedObject& o = out.emplace_back();
                o.id = ++last_id;
                o.owner_ref = o.id;
                o.fixture = true;
                o.symbol = piece.symbol;
                o.overlay = overlay;
                o.col = square.col;
                o.row = square.row;
                o.base_height = piece.height;
                o.force_absolute_height = true;
                o.rotation = piece.turn;
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
            o.rotation = e.turn;
        }
        for (const PoolRoom& r : map.rooms)
        {
            const auto square = beside(map, r.col, r.row, r.turn);
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
            o.rotation = (r.turn + 1) & 3;
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

    std::int64_t pool_build_cost(std::span<const std::int32_t, 18> prices, const PoolSquare& square) noexcept
    {
        return price(prices, square, kBuild);
    }

    std::int64_t pool_refund(std::span<const std::int32_t, 18> prices, const PoolSquare& square) noexcept
    {
        return price(prices, square, kRefund);
    }

    bool pool_room_fits(const PoolMap& map, std::int32_t col, std::int32_t row, std::int32_t turn)
    {
        const auto square = beside(map, col, row, turn);
        const auto [dc, dr] = kSideStep[static_cast<std::size_t>(turn & 3)];
        const bool taken = std::ranges::any_of(map.rooms, [&](const PoolRoom& r) {
            const auto [rc, rr] = kSideStep[static_cast<std::size_t>(r.turn & 3)];
            return (r.col == col && r.row == row) || (r.col + rc == col + dc && r.row + rr == row + dr);
        });
        return !map.at(col, row) && square && square->kind == PoolKind::Tile && !taken;
    }

    std::vector<save::Tile> pool_complex(const PoolMap& map, std::size_t room)
    {
        std::vector<save::Tile> complex;
        if (room >= map.rooms.size())
            return { };
        if (const auto first = beside(map, map.rooms[room].col, map.rooms[room].row, map.rooms[room].turn))
            complex.push_back({ first->col, first->row });
        for (std::size_t i = 0; i < complex.size(); ++i)
            for (std::int32_t turn = 0; turn < 4; ++turn)
                if (const auto next = beside(map, complex[i].col, complex[i].row, turn); next && !std::ranges::contains(complex, save::Tile { next->col, next->row }))
                    complex.push_back({ next->col, next->row });
        return complex;
    }

    std::vector<std::size_t> pool_complex_rooms(const PoolMap& map, std::size_t room)
    {
        const std::vector<save::Tile> complex = pool_complex(map, room);
        std::vector<std::size_t> out;
        for (std::size_t i = 0; i < map.rooms.size(); ++i)
            if (const auto square = beside(map, map.rooms[i].col, map.rooms[i].row, map.rooms[i].turn); square && std::ranges::contains(complex, save::Tile { square->col, square->row }))
                out.push_back(i);
        return out;
    }

    std::int64_t lay_pool(PoolMap& map, std::span<const std::int32_t, 18> prices, std::span<const PoolSquare> squares)
    {
        std::int64_t cost = 0;
        for (const PoolSquare& square : squares)
        {
            const auto was = std::ranges::find_if(map.squares, [&](const PoolSquare& s) { return s.col == square.col && s.row == square.row; });
            if (was == map.squares.end())
            {
                cost += pool_build_cost(prices, square);
                map.squares.push_back(square);
            }
            else if (!same(*was, square))
            {
                cost += pool_build_cost(prices, square) + pool_refund(prices, *was);
                *was = square;
                cost += clear_extras(map, square.col, square.row);
            }
            else
                was->edge = square.edge; // A new edge setting alone costs nothing
        }
        drop_loose_rooms(map);
        return cost;
    }

    std::int64_t take_up_pool(PoolMap& map, std::span<const std::int32_t, 18> prices, save::Tile a, save::Tile b)
    {
        const auto [c0, c1] = std::minmax(a.col, b.col);
        const auto [r0, r1] = std::minmax(a.row, b.row);
        std::int64_t cost = 0;
        std::erase_if(map.squares, [&](const PoolSquare& s) {
            if (s.col < c0 || s.col > c1 || s.row < r0 || s.row > r1)
                return false;
            cost += pool_refund(prices, s);
            return true;
        });
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
        constexpr std::int32_t kWater = 0, kHighDive = 3, kSpa = 4, kPath = 5, kPathSlope = 6, kPathStairs = 7; // Of the stored types
        PoolMap map;
        for (const save::PoolSegmentState& s : world.pool_segments)
        {
            PoolSquare& square = map.squares.emplace_back(PoolSquare { .col = s.col, .row = s.row, .surface = s.surface, .depth = s.depth });
            square.kind = s.type == kWater ? PoolKind::Water : s.type == kSpa ? PoolKind::Jacuzzi : s.type == kHighDive ? PoolKind::HighDive : s.type == kPath ? PoolKind::Path : s.type == kPathSlope ? PoolKind::PathSlope : s.type == kPathStairs ? PoolKind::PathStairs : PoolKind::Tile;
            square.board = s.type == kHighDive ? s.special : 0;
        }
        // A board faces the water beside it; a path's slope or stairs has its foot at the path beside it level
        // with it.
        for (PoolSquare& square : map.squares)
        {
            const bool climbs = square.kind == PoolKind::PathSlope || square.kind == PoolKind::PathStairs;
            for (std::int32_t turn = 0; (square.kind == PoolKind::HighDive || climbs) && turn < 4; ++turn)
                if (const auto next = map.at(square.col + kSideStep[static_cast<std::size_t>(turn)][0], square.row + kSideStep[static_cast<std::size_t>(turn)][1]); next && (climbs ? next->kind == PoolKind::Path && next->surface == square.surface : next->kind == PoolKind::Water))
                {
                    square.turn = turn;
                    break;
                }
        }
        for (const save::ChangingRoomState& room : world.changing_rooms)
        {
            const auto object = std::ranges::find(objects, room.object_ref, &save::PlacedObject::id);
            const auto segment = std::ranges::find(world.pool_segments, room.segment, &save::PoolSegmentState::id);
            if (object == objects.end() || segment == world.pool_segments.end())
                continue;
            const auto turn = std::ranges::find(kSideStep, std::array { segment->col - object->col, segment->row - object->row });
            if (turn == kSideStep.end())
                continue;
            const auto complex = std::ranges::find(world.pool_complexes, segment->complex, &save::PoolComplexState::index);
            const bool named = complex != world.pool_complexes.end() && !complex->name.starts_with("%ME%");
            map.rooms.push_back({ .symbol = object->symbol, .overlay = object->overlay, .col = object->col, .row = object->row, .turn = static_cast<std::int32_t>(turn - kSideStep.begin()), .ticket = complex != world.pool_complexes.end() ? complex->ticket : kPoolTicket, .open = room.open, .complex_name = named ? complex->name : std::string(), .fluid = static_cast<std::uint8_t>(std::clamp(room.fluid, 0, 2)) });
        }
        return map;
    }

} // namespace rct3::park
