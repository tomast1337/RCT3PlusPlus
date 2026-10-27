// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/pools.hpp"

#include <algorithm>
#include <bit>

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

    } // namespace

    std::optional<const PoolSquare&> PoolMap::at(std::int32_t col, std::int32_t row) const noexcept
    {
        const auto found = std::ranges::find_if(squares, [&](const PoolSquare& s) { return s.col == col && s.row == row; });
        if (found == squares.end())
            return std::nullopt;
        return *found;
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
            // Turned as its middle side, the one across from the water.
            for (std::int32_t turn = 0; turn < 4; ++turn)
                if (!open(turn))
                    return { { PoolWall::End, (turn + 2) & 3 } };
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
            if (const auto next = map.at(square.col + dc, square.row + dr); !next || next->surface != square.surface)
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
            }
        }
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
        return cost;
    }

} // namespace rct3::park
