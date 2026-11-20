// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/enclosures.hpp"

#include "core/types.hpp"
#include "park/ground/queues.hpp"
#include "park/queue_fences.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <format>
#include <optional>
#include <vector>

namespace rct3::park
{

    namespace
    {
        /// The side of a tile a piece turned `turns` quarter turns fences.
        Side side_of(i32 turns) noexcept
        {
            for (const Side side : ground::kSidesFromRowBefore)
                if (edge_turns(side) == ModelTurn::from(turns))
                    return side;
            return Side::kRowBefore;
        }

        /// The tile across `side` of (`col`, `row`).
        save::model::Tile across(i32 col, i32 row, Side side) noexcept
        {
            return side.beyond({ col, row });
        }

        /// The fence along `side` of (`col`, `row`) in `map`, if one stands there.
        std::optional<std::string_view> fenced(const save::model::EnclosureMap& map, i32 col, i32 row, Side side)
        {
            const std::optional<i32> own = map.at(col, row);
            const save::model::Tile other = across(col, row, side);
            if (!own || static_cast<usize>(*own) >= map.fences.size() || map.at(other.col, other.row) == own)
                return std::nullopt;
            return map.fences[static_cast<usize>(*own)];
        }

        /// Bit t: the side a piece turned t quarter turns fences faces out of the tile's enclosure.
        u32 sides_out(const save::model::EnclosureMap& map, i32 col, i32 row)
        {
            u32 out = 0;
            for (i32 t = 0; t < 4; ++t)
                if (fenced(map, col, row, side_of(t)))
                    out |= 1u << t;
            return out;
        }

        /// Makes each connected area of tiles with the same fence one enclosure. Each area keeps the place of
        /// the enclosure most of its tiles were in, the larger areas choosing first; the others take free places.
        /// A tile whose fence is no longer straight panels, or that has none, has it as new: the pieces were put up again.
        void regroup(save::model::EnclosureMap& map)
        {
            struct Area
            {
                std::string_view fence;
                i32 size = 0;
                std::vector<i32> was { }; ///< Tiles by the place they were in
            };
            std::vector<std::optional<i32>> area_of(map.tiles.size());
            std::vector<Area> areas;
            std::vector<i32> open;
            for (usize first = 0; first < map.tiles.size(); ++first)
            {
                if (!map.tiles[first] || area_of[first])
                    continue;
                const auto index = static_cast<i32>(areas.size());
                Area& area = areas.emplace_back(Area { .fence = map.fences[static_cast<usize>(*map.tiles[first])], .was = std::vector<i32>(map.fences.size()) });
                area_of[first] = index;
                open.assign(1, static_cast<i32>(first));
                while (!open.empty())
                {
                    const i32 k = open.back();
                    open.pop_back();
                    ++area.size;
                    ++area.was[static_cast<usize>(*map.tiles[static_cast<usize>(k)])];
                    for (const Side side : ground::kSidesFromRowBefore)
                    {
                        const save::model::Tile t = across(k % map.cols, k / map.cols, side);
                        const std::optional<i32> other = map.at(t.col, t.row);
                        const i32 j = t.row * map.cols + t.col;
                        if (other && !area_of[static_cast<usize>(j)] && map.fences[static_cast<usize>(*other)] == area.fence)
                        {
                            area_of[static_cast<usize>(j)] = index;
                            open.push_back(j);
                        }
                    }
                }
            }
            std::vector<usize> order(areas.size());
            for (usize i = 0; i < order.size(); ++i)
                order[i] = i;
            std::ranges::stable_sort(order, std::greater { }, [&](usize i) { return areas[i].size; });
            std::vector<bool> claimed(map.fences.size() + areas.size());
            std::vector<std::optional<i32>> place(areas.size());
            for (const usize i : order)
            {
                i32 most = 0;
                for (usize r = 0; r < areas[i].was.size(); ++r)
                    if (!claimed[r] && areas[i].was[r] > most)
                    {
                        most = areas[i].was[r];
                        place[i] = static_cast<i32>(r);
                    }
                if (place[i])
                    claimed[static_cast<usize>(*place[i])] = true;
            }
            for (const usize i : order)
            {
                if (place[i])
                    continue;
                place[i] = static_cast<i32>(std::ranges::find(claimed, false) - claimed.begin());
                claimed[static_cast<usize>(*place[i])] = true;
            }
            std::vector<std::string> fences(map.fences.size());
            for (usize i = 0; i < areas.size(); ++i)
            {
                const auto p = static_cast<usize>(*place[i]);
                fences.resize(std::max(fences.size(), p + 1));
                fences[p] = areas[i].fence;
            }
            for (usize k = 0; k < map.tiles.size(); ++k)
                if (area_of[k])
                    map.tiles[k] = place[static_cast<usize>(*area_of[k])];
            map.fences = std::move(fences);
            for (usize k = 0; k < map.wear.size(); ++k)
                if (map.wear[k] != save::model::FenceWear::Sound && !panelled(map, { static_cast<i32>(k % static_cast<usize>(map.cols)), static_cast<i32>(k / static_cast<usize>(map.cols)) }))
                    map.wear[k] = save::model::FenceWear::Sound;
        }

        /// The least and greatest column and row from `a` to `b`, within `map`.
        std::array<i32, 4> clamped(const save::model::EnclosureMap& map, save::model::Tile a, save::model::Tile b)
        {
            return { std::max(0, std::min(a.col, b.col)), std::max(0, std::min(a.row, b.row)), std::min(map.cols - 1, std::max(a.col, b.col)), std::min(map.rows - 1, std::max(a.row, b.row)) };
        }
    } // namespace

    std::string fence_overlay(std::string_view fence)
    {
        return std::format("Enclosures/Fences/{0}/{0}_Stub", fence);
    }

    BoundaryFence enclosure_fences(const save::model::EnclosureMap& map, const save::model::TerrainGrid& grid, u64 last_id)
    {
        BoundaryFence out;
        for (i32 row = 0; row < map.rows; ++row)
        {
            for (i32 col = 0; col < map.cols; ++col)
            {
                const std::optional<i32> own = map.at(col, row);
                if (!own)
                    continue;
                const std::string& fence = map.fences[static_cast<usize>(*own)];
                const auto add = [&](std::string_view piece, i32 turns) {
                    save::model::PlacedObject& o = out.pieces.emplace_back();
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
                const u32 out_sides = sides_out(map, col, row);
                const auto turns_of = [](u32 bit) { return static_cast<i32>(std::countr_zero(bit)); };
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
                for (i32 t = 0; t < 4; ++t)
                {
                    if ((out_sides & (1u << t)) != 0 || (out_sides & (1u << ((t + 1) % 4))) != 0)
                        continue;
                    const save::model::Tile side = across(col, row, side_of(t));
                    const save::model::Tile corner = across(side.col, side.row, side_of(t + 1));
                    if (map.at(corner.col, corner.row) != own)
                        add("CornerI", t);
                }
            }
        }
        return out;
    }

    glm::vec2 fence_piece_lean(const save::model::TerrainGrid& grid, const save::model::PlacedObject& piece)
    {
        if (piece.symbol.ends_with("Edge"))
            return panel_lean(grid, piece);
        // A piece round more than one side leans with both sides at its corner.
        return edge_lean(grid, { piece.col, piece.row }, side_of(piece.rotation)) + edge_lean(grid, { piece.col, piece.row }, side_of(piece.rotation + 1));
    }

    std::optional<i32> enclose(save::model::EnclosureMap& map, const save::model::TerrainGrid& grid, save::model::Tile a, save::model::Tile b, const std::string& fence, std::function_ref<bool(save::model::Tile)> takeable)
    {
        if (map.tiles.empty())
            map = { .cols = grid.width, .rows = grid.height, .tiles = std::vector<std::optional<i32>>(static_cast<usize>(grid.width) * static_cast<usize>(grid.height)), .fences = map.fences };
        // The new tiles go in a free place first, so an area that joins nothing keeps it.
        const auto unused = std::ranges::find(map.fences, std::string());
        const auto region = static_cast<i32>(unused - map.fences.begin());
        if (unused == map.fences.end())
            map.fences.push_back(fence);
        else
            *unused = fence;
        std::optional<usize> first;
        const auto [c0, r0, c1, r1] = clamped(map, a, b);
        for (i32 row = r0; row <= r1; ++row)
            for (i32 col = c0; col <= c1; ++col)
                if (std::optional<i32>& t = map.tiles[static_cast<usize>(row * map.cols + col)]; !t && takeable({ col, row }))
                {
                    t.emplace(region);
                    first = first.value_or(static_cast<usize>(row * map.cols + col));
                }
        regroup(map);
        return first ? map.tiles[*first] : std::nullopt;
    }

    void unenclose(save::model::EnclosureMap& map, save::model::Tile a, save::model::Tile b, std::function_ref<bool(save::model::Tile)> removable)
    {
        const auto [c0, r0, c1, r1] = clamped(map, a, b);
        for (i32 row = r0; row <= r1; ++row)
            for (i32 col = c0; col <= c1; ++col)
                if (std::optional<i32>& t = map.tiles[static_cast<usize>(row * map.cols + col)]; t && removable({ col, row }))
                    t.reset();
        regroup(map);
    }

    units::Money fence_cost(const save::model::EnclosureMap& before, const save::model::EnclosureMap& after, std::function_ref<FencePrice(std::string_view fence)> price)
    {
        units::Money cost { };
        for (i32 row = 0; row < std::max(before.rows, after.rows); ++row)
        {
            for (i32 col = 0; col < std::max(before.cols, after.cols); ++col)
            {
                for (const Side side : ground::kSidesFromRowBefore)
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

    bool panelled(const save::model::EnclosureMap& map, save::model::Tile tile)
    {
        const u32 out = sides_out(map, tile.col, tile.row);
        return std::popcount(out) == 1 || out == 0b0101 || out == 0b1010;
    }

    i32 enclosure_size(const save::model::EnclosureMap& map, std::optional<i32> region) noexcept
    {
        return static_cast<i32>(std::ranges::count(map.tiles, region));
    }

    std::optional<i32> enclosure_under(const save::model::EnclosureMap& map, save::model::Tile corner, std::array<i32, 2> span) noexcept
    {
        const std::optional<i32> first = map.at(corner.col, corner.row);
        for (i32 row = corner.row; row < corner.row + span[1]; ++row)
            for (i32 col = corner.col; col < corner.col + span[0]; ++col)
                if (map.at(col, row) != first)
                    return std::nullopt;
        return first;
    }

} // namespace rct3::park
