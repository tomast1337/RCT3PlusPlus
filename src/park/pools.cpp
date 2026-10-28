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

        /// The piece names of one wall shape, from the bottom of a stack up.
        constexpr std::array<std::array<std::string_view, 3>, 5> kWallPieces { {
            { "EdgeBot", "EdgeMid", "EdgeTop" },
            { "Corner2Bot", "Corner2Mid", "Corner2Top" },
            { "Corner3Bot", "Corner3Mid", "Corner3Top" },
            { "EndBot", "EndMid", "EndTop" },
            { "FullBot", "FullMid", "FullTop" },
        } };

        /// What one square costs to lay or to take up, from the pool type's price list
        /// (ovl::PoolType::prices, by index): a part for its kind, another for each unit of its depth, and
        /// more for a spa pool.
        struct PriceFields
        {
            std::size_t base, per_depth, jacuzzi;
        };
        constexpr PriceFields kBuild[] { { 2, 3, 0 }, { 6, 7, 12 } };  // Water, paved
        constexpr PriceFields kRefund[] { { 4, 5, 0 }, { 8, 9, 13 } }; // The same, when taken up

        std::int64_t price(std::span<const std::int32_t, 18> prices, const PoolSquare& square, const PriceFields (&fields)[2]) noexcept
        {
            const PriceFields& f = fields[square.kind == PoolKind::Water ? 0 : 1];
            return prices[f.base] + std::int64_t { prices[f.per_depth] } * square.depth + (square.kind == PoolKind::Jacuzzi ? prices[f.jacuzzi] : 0);
        }

        bool same(const PoolSquare& a, const PoolSquare& b) noexcept
        {
            return a.kind == b.kind && a.depth == b.depth && a.surface == b.surface;
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
        const std::int32_t floor = square.surface - square.depth;
        const auto stack = [&](const std::array<std::string_view, 3>& names, std::int32_t turn) {
            for (std::int32_t h = floor; h <= square.surface; ++h)
                out.push_back({ names[h == floor ? 0 : h == square.surface ? 2 : 1], h, turn });
        };
        if (square.kind == PoolKind::Water)
        {
            out.push_back({ "Base", floor, 0 });
            for (const PoolWallPiece& wall : pool_walls(water_around(map, square)))
                stack(kWallPieces[static_cast<std::size_t>(wall.shape)], wall.turn);
        }
        else
        {
            stack({ "SolidBot", "SolidMid", "SolidTop" }, 0);
            if (square.kind == PoolKind::Jacuzzi)
                out.back().symbol = "Jacuzzi";
        }
        for (std::int32_t turn = 0; turn < 4; ++turn)
        {
            const auto [dc, dr] = kSideStep[static_cast<std::size_t>(turn)];
            const bool room = std::ranges::any_of(map.rooms, [&](const PoolRoom& r) { return r.col == square.col + dc && r.row == square.row + dr && ((r.turn + 2) & 3) == turn; });
            if (const auto next = map.at(square.col + dc, square.row + dr); !room && (!next || next->surface != square.surface))
                out.push_back({ "Railing", square.surface, turn });
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

} // namespace rct3::park
