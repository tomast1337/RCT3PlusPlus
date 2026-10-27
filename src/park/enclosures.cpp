// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/enclosures.hpp"

#include "park/ground_queues.hpp"
#include "park/queue_fences.hpp"

#include <algorithm>
#include <bit>
#include <format>
#include <optional>
#include <vector>

namespace rct3::park
{

    namespace
    {
        constexpr std::array kSides { TileSide::Back, TileSide::Right, TileSide::Front, TileSide::Left };

        /// The side of a tile a piece turned `turns` quarter turns fences.
        TileSide side_of(std::int32_t turns) noexcept
        {
            for (const TileSide side : kSides)
                if (edge_turns(side) == ((turns % 4) + 4) % 4)
                    return side;
            return TileSide::Back;
        }

        /// The tile across `side` of (`col`, `row`).
        save::Tile across(std::int32_t col, std::int32_t row, TileSide side) noexcept
        {
            const auto [dc, dr] = side_step(side);
            return { col + dc, row + dr };
        }

        /// The fence along `side` of (`col`, `row`) in `map`, if one stands there.
        std::optional<std::string_view> fenced(const save::EnclosureMap& map, std::int32_t col, std::int32_t row, TileSide side)
        {
            const std::int32_t own = map.at(col, row);
            const save::Tile other = across(col, row, side);
            if (own < 0 || static_cast<std::size_t>(own) >= map.fences.size() || map.at(other.col, other.row) == own)
                return std::nullopt;
            return map.fences[static_cast<std::size_t>(own)];
        }

        /// Makes each connected area of tiles with the same fence one enclosure. Each area keeps the place of
        /// the enclosure most of its tiles were in, the larger areas choosing first; the others take free places.
        void regroup(save::EnclosureMap& map)
        {
            struct Area
            {
                std::string_view fence;
                std::int32_t size = 0;
                std::vector<std::int32_t> was { }; ///< Tiles by the place they were in
            };
            std::vector<std::int32_t> area_of(map.tiles.size(), -1);
            std::vector<Area> areas;
            std::vector<std::int32_t> open;
            for (std::size_t first = 0; first < map.tiles.size(); ++first)
            {
                if (map.tiles[first] < 0 || area_of[first] >= 0)
                    continue;
                const auto index = static_cast<std::int32_t>(areas.size());
                Area& area = areas.emplace_back(Area { .fence = map.fences[static_cast<std::size_t>(map.tiles[first])], .was = std::vector<std::int32_t>(map.fences.size()) });
                area_of[first] = index;
                open.assign(1, static_cast<std::int32_t>(first));
                while (!open.empty())
                {
                    const std::int32_t k = open.back();
                    open.pop_back();
                    ++area.size;
                    ++area.was[static_cast<std::size_t>(map.tiles[static_cast<std::size_t>(k)])];
                    for (const TileSide side : kSides)
                    {
                        const save::Tile t = across(k % map.cols, k / map.cols, side);
                        const std::int32_t other = map.at(t.col, t.row);
                        const std::int32_t j = t.row * map.cols + t.col;
                        if (other >= 0 && area_of[static_cast<std::size_t>(j)] < 0 && map.fences[static_cast<std::size_t>(other)] == area.fence)
                        {
                            area_of[static_cast<std::size_t>(j)] = index;
                            open.push_back(j);
                        }
                    }
                }
            }
            std::vector<std::size_t> order(areas.size());
            for (std::size_t i = 0; i < order.size(); ++i)
                order[i] = i;
            std::ranges::stable_sort(order, std::greater { }, [&](std::size_t i) { return areas[i].size; });
            std::vector<bool> claimed(map.fences.size() + areas.size());
            std::vector<std::int32_t> place(areas.size(), -1);
            for (const std::size_t i : order)
            {
                std::int32_t most = 0;
                for (std::size_t r = 0; r < areas[i].was.size(); ++r)
                    if (!claimed[r] && areas[i].was[r] > most)
                    {
                        most = areas[i].was[r];
                        place[i] = static_cast<std::int32_t>(r);
                    }
                if (place[i] >= 0)
                    claimed[static_cast<std::size_t>(place[i])] = true;
            }
            for (const std::size_t i : order)
            {
                if (place[i] >= 0)
                    continue;
                place[i] = static_cast<std::int32_t>(std::ranges::find(claimed, false) - claimed.begin());
                claimed[static_cast<std::size_t>(place[i])] = true;
            }
            std::vector<std::string> fences(map.fences.size());
            for (std::size_t i = 0; i < areas.size(); ++i)
            {
                const auto p = static_cast<std::size_t>(place[i]);
                fences.resize(std::max(fences.size(), p + 1));
                fences[p] = areas[i].fence;
            }
            for (std::size_t k = 0; k < map.tiles.size(); ++k)
                if (area_of[k] >= 0)
                    map.tiles[k] = place[static_cast<std::size_t>(area_of[k])];
            map.fences = std::move(fences);
        }

        /// The least and greatest column and row from `a` to `b`, within `map`.
        std::array<std::int32_t, 4> clamped(const save::EnclosureMap& map, save::Tile a, save::Tile b)
        {
            return { std::max(0, std::min(a.col, b.col)), std::max(0, std::min(a.row, b.row)), std::min(map.cols - 1, std::max(a.col, b.col)), std::min(map.rows - 1, std::max(a.row, b.row)) };
        }
    } // namespace

    std::string fence_overlay(std::string_view fence)
    {
        return std::format("Enclosures/Fences/{0}/{0}_Stub", fence);
    }

    BoundaryFence enclosure_fences(const save::EnclosureMap& map, const save::TerrainGrid& grid, std::uint64_t last_id)
    {
        BoundaryFence out;
        for (std::int32_t row = 0; row < map.rows; ++row)
        {
            for (std::int32_t col = 0; col < map.cols; ++col)
            {
                const std::int32_t own = map.at(col, row);
                if (own < 0)
                    continue;
                const std::string& fence = map.fences[static_cast<std::size_t>(own)];
                const auto add = [&](std::string_view piece, std::int32_t turns) {
                    save::PlacedObject& o = out.pieces.emplace_back();
                    o.id = ++last_id;
                    o.owner_ref = o.id;
                    o.fixture = true;
                    o.symbol = fence + std::string(piece);
                    o.overlay = fence_overlay(fence);
                    o.col = col;
                    o.row = row;
                    o.rotation = turns;
                    if (grid.contains(col, row))
                        if (const glm::vec2 lean = fence_piece_lean(grid, o); lean != glm::vec2(0.0f))
                            out.leans.emplace(o.id, lean);
                };
                // Bit t: the side a piece turned t quarter turns fences faces out of the enclosure.
                unsigned out_sides = 0;
                for (std::int32_t t = 0; t < 4; ++t)
                    if (fenced(map, col, row, side_of(t)))
                        out_sides |= 1u << t;
                const auto turns_of = [](unsigned bit) { return static_cast<std::int32_t>(std::countr_zero(bit)); };
                switch (std::popcount(out_sides))
                {
                    case 1:
                        add("Edge", turns_of(out_sides));
                        break;
                    case 2:
                        if (out_sides == 0b0101 || out_sides == 0b1010)
                        {
                            add("Edge", turns_of(out_sides));
                            add("Edge", turns_of(out_sides) + 2);
                        }
                        else // Two sides meeting: the corner piece turned to the first of them going round
                            add("Corner", out_sides == 0b1001 ? 3 : turns_of(out_sides));
                        break;
                    case 3:
                        add("End", turns_of(~out_sides & 0b1111)); // Turned to its open side
                        break;
                    case 4:
                        add("Full", 0);
                        break;
                    default:
                        break;
                }
                // A post in each inside corner: both sides there in the enclosure, the tile across the corner not.
                for (std::int32_t t = 0; t < 4; ++t)
                {
                    if ((out_sides & (1u << t)) != 0 || (out_sides & (1u << ((t + 1) % 4))) != 0)
                        continue;
                    const save::Tile side = across(col, row, side_of(t));
                    const save::Tile corner = across(side.col, side.row, side_of(t + 1));
                    if (map.at(corner.col, corner.row) != own)
                        add("CornerI", t);
                }
            }
        }
        return out;
    }

    glm::vec2 fence_piece_lean(const save::TerrainGrid& grid, const save::PlacedObject& piece)
    {
        if (piece.symbol.ends_with("Edge"))
            return panel_lean(grid, piece);
        // A piece round more than one side leans with both sides at its corner.
        return edge_lean(grid, { piece.col, piece.row }, side_of(piece.rotation)) + edge_lean(grid, { piece.col, piece.row }, side_of(piece.rotation + 1));
    }

    std::int32_t enclose(save::EnclosureMap& map, const save::TerrainGrid& grid, save::Tile a, save::Tile b, const std::string& fence, std::function_ref<bool(save::Tile)> takeable)
    {
        if (map.tiles.empty())
            map = { .cols = grid.width, .rows = grid.height, .tiles = std::vector<std::int32_t>(static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height), -1), .fences = map.fences };
        // The new tiles go in a free place first, so an area that joins nothing keeps it.
        const auto unused = std::ranges::find(map.fences, std::string());
        const auto region = static_cast<std::int32_t>(unused - map.fences.begin());
        if (unused == map.fences.end())
            map.fences.push_back(fence);
        else
            *unused = fence;
        std::optional<std::size_t> first;
        const auto [c0, r0, c1, r1] = clamped(map, a, b);
        for (std::int32_t row = r0; row <= r1; ++row)
            for (std::int32_t col = c0; col <= c1; ++col)
                if (std::int32_t& t = map.tiles[static_cast<std::size_t>(row * map.cols + col)]; t < 0 && takeable({ col, row }))
                {
                    t = region;
                    first = first.value_or(static_cast<std::size_t>(row * map.cols + col));
                }
        regroup(map);
        return first ? map.tiles[*first] : -1;
    }

    void unenclose(save::EnclosureMap& map, save::Tile a, save::Tile b, std::function_ref<bool(save::Tile)> removable)
    {
        const auto [c0, r0, c1, r1] = clamped(map, a, b);
        for (std::int32_t row = r0; row <= r1; ++row)
            for (std::int32_t col = c0; col <= c1; ++col)
                if (std::int32_t& t = map.tiles[static_cast<std::size_t>(row * map.cols + col)]; t >= 0 && removable({ col, row }))
                    t = -1;
        regroup(map);
    }

    std::int64_t fence_cost(const save::EnclosureMap& before, const save::EnclosureMap& after, std::function_ref<FencePrice(std::string_view fence)> price)
    {
        std::int64_t cost = 0;
        for (std::int32_t row = 0; row < std::max(before.rows, after.rows); ++row)
        {
            for (std::int32_t col = 0; col < std::max(before.cols, after.cols); ++col)
            {
                for (const TileSide side : kSides)
                {
                    const auto was = fenced(before, col, row, side);
                    const auto is = fenced(after, col, row, side);
                    if (was == is)
                        continue;
                    if (is)
                        cost += price(*is).build;
                    if (was)
                        cost += price(*was).refund;
                }
            }
        }
        return cost;
    }

    std::int32_t enclosure_size(const save::EnclosureMap& map, std::int32_t region) noexcept
    {
        return static_cast<std::int32_t>(std::ranges::count(map.tiles, region));
    }

    std::int32_t enclosure_under(const save::EnclosureMap& map, save::Tile corner, std::array<std::int32_t, 2> span) noexcept
    {
        const std::int32_t first = map.at(corner.col, corner.row);
        for (std::int32_t row = corner.row; row < corner.row + span[1]; ++row)
            for (std::int32_t col = corner.col; col < corner.col + span[0]; ++col)
                if (map.at(col, row) != first)
                    return -1;
        return first;
    }

} // namespace rct3::park
