// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/water/fill.hpp"

#include "park/waterfall_making.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace rct3::park::water
{

    namespace
    {
        bool below(const save::model::TerrainCell& cell, std::size_t a, std::size_t b, float level)
        {
            return cell.corners[a] < level || cell.corners[b] < level;
        }

        enum class Crossing : std::uint8_t
        {
            Flows,
            Stops, ///< As at higher ground
            Leaks, ///< The flood fails
        };

        /// The flood of fill_water, asking `crossing` before the water goes from one tile to the next.
        template<typename Cross> std::optional<save::model::WaterBody> flood(const save::model::TerrainGrid& grid, save::model::Tile tile, float level, Cross&& crossing)
        {
            const auto [col, row] = tile;
            save::model::WaterBody body;
            body.level = level;
            if (!grid.contains(col, row) || grid.at(col, row).min_height() >= level)
                return body;
            // Each way to a neighbour: the step, and the corners of the shared edge on this side and on the far one.
            struct Way
            {
                int dc, dr;
                std::size_t a, b, far_a, far_b;
            };
            constexpr std::array<Way, 4> kWays { {
                { 1, 0, 1, 3, 0, 2 },
                { -1, 0, 0, 2, 1, 3 },
                { 0, 1, 2, 3, 0, 1 },
                { 0, -1, 0, 1, 2, 3 },
            } };
            std::vector<bool> seen(grid.cells.size());
            std::vector<std::pair<int, int>> open { { col, row } };
            seen[static_cast<std::size_t>(row * grid.width + col)] = true;
            while (!open.empty())
            {
                const auto [c, r] = open.back();
                open.pop_back();
                for (int t = 0; t < 2; ++t)
                {
                    if (const auto mask = save::model::expected_water_mask(grid, level, { c, r }, t); mask && *mask != 0)
                        body.tris.push_back({ static_cast<std::uint8_t>(c), static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(t), *mask });
                }
                const save::model::TerrainCell& cell = grid.at(c, r);
                for (const Way& w : kWays)
                {
                    const int nc = c + w.dc;
                    const int nr = r + w.dr;
                    if (!grid.contains(nc, nr) || seen[static_cast<std::size_t>(nr * grid.width + nc)])
                        continue;
                    if (!below(cell, w.a, w.b, level) || !below(grid.at(nc, nr), w.far_a, w.far_b, level))
                        continue;
                    const Crossing way = crossing(save::model::Tile { c, r }, save::model::Tile { nc, nr });
                    if (way == Crossing::Leaks)
                        return std::nullopt;
                    if (way == Crossing::Stops)
                        continue;
                    seen[static_cast<std::size_t>(nr * grid.width + nc)] = true;
                    open.emplace_back(nc, nr);
                }
            }
            return body;
        }
    } // namespace

    std::optional<save::model::WaterBody> fill_water(const save::model::TerrainGrid& grid, save::model::Tile tile, float level, const std::function<bool(save::model::Tile)>& allowed)
    {
        if (grid.contains(tile.col, tile.row) && grid.at(tile.col, tile.row).min_height() < level && !allowed(tile))
            return std::nullopt;
        return flood(grid, tile, level, [&](save::model::Tile, save::model::Tile to) { return allowed(to) ? Crossing::Flows : Crossing::Leaks; });
    }

    std::optional<save::model::WaterLayer> pour_water(const save::model::WaterLayer& water, const save::model::TerrainGrid& grid, save::model::Tile tile, float level, const std::function<bool(save::model::Tile)>& allowed)
    {
        const auto lake = fill_water(grid, tile, std::ceil(level), allowed);
        if (!lake || lake->tris.empty())
            return lake ? std::optional { water } : std::nullopt;
        const auto reached = [&](const save::model::WaterBody& b) { return std::ranges::any_of(lake->tris, [&](const save::model::WaterTri& t) { return holds(b, { t.col, t.row }); }); };
        if (std::ranges::any_of(water.bodies, [&](const save::model::WaterBody& b) { return b.level > lake->level && reached(b); }))
            return std::nullopt;
        save::model::WaterLayer poured = water;
        std::erase_if(poured.bodies, reached);
        poured.bodies.push_back(*lake);
        return poured;
    }

    bool remove_lake(save::model::WaterLayer& water, std::vector<save::model::Waterfall>& falls, save::model::TerrainGrid& grid, save::model::Tile tile)
    {
        const auto lake = std::ranges::find_if(water.bodies, [&](const save::model::WaterBody& b) { return holds(b, tile); });
        if (lake == water.bodies.end())
            return false;
        const auto on_lake = [&](const save::model::WorldPoint3& p) { return holds(*lake, { static_cast<int>(std::floor((p.x - grid.origin_x) / grid.tile_size_x)), static_cast<int>(std::floor((p.z - grid.origin_z) / grid.tile_size_z)) }); };
        std::erase_if(falls, [&](const save::model::Waterfall& f) {
            if (!on_lake(f.top) && !on_lake(f.bottom))
                return false;
            restore_spillway(grid, f);
            return true;
        });
        water.bodies.erase(lake);
        return true;
    }

    std::vector<std::size_t> flooded_falls(const save::model::WaterLayer& was, const save::model::WaterLayer& now, std::span<const save::model::Waterfall> falls, const save::model::TerrainGrid& grid)
    {
        const auto level = [&](const save::model::WaterLayer& water, const save::model::WorldPoint3& p) -> std::optional<float> {
            const save::model::Tile tile { static_cast<int>(std::floor((p.x - grid.origin_x) / grid.tile_size_x)), static_cast<int>(std::floor((p.z - grid.origin_z) / grid.tile_size_z)) };
            const auto lake = std::ranges::find_if(water.bodies, [&](const save::model::WaterBody& b) { return holds(b, tile); });
            return lake == water.bodies.end() ? std::nullopt : std::optional(lake->level);
        };
        std::vector<std::size_t> out;
        for (std::size_t i = 0; i < falls.size(); ++i)
        {
            if (level(was, falls[i].top) != level(now, falls[i].top) || level(was, falls[i].bottom) != level(now, falls[i].bottom))
                out.push_back(i);
        }
        return out;
    }

    bool settle_water(save::model::WaterLayer& water, std::vector<save::model::Waterfall>& falls, const save::model::TerrainGrid& was, const save::model::TerrainGrid& grid)
    {
        const auto at = [&](int col, int row) { return static_cast<std::size_t>(row * grid.width + col); };
        std::vector<bool> moved(grid.cells.size(), false);
        bool any_moved = false;
        for (int r = 0; r < grid.height; ++r)
            for (int c = 0; c < grid.width; ++c)
            {
                if (was.contains(c, r) && was.at(c, r).corners != grid.at(c, r).corners)
                    moved[at(c, r)] = any_moved = true;
            }
        if (!any_moved)
            return false;
        const auto tile_of = [&](const save::model::WorldPoint3& p) { return save::model::Tile { static_cast<int>(std::floor((p.x - grid.origin_x) / grid.tile_size_x)), static_cast<int>(std::floor((p.z - grid.origin_z) / grid.tile_size_z)) }; };
        // The level of the water over a tile, to tell which waterfalls lost an end.
        const auto level_over = [&](save::model::Tile tile) -> std::optional<float> {
            const auto lake = std::ranges::find_if(water.bodies, [&](const save::model::WaterBody& b) { return holds(b, tile); });
            return lake == water.bodies.end() ? std::nullopt : std::optional(lake->level);
        };
        std::vector<std::array<std::optional<float>, 2>> ends;
        for (const save::model::Waterfall& f : falls)
            ends.push_back({ level_over(tile_of(f.top)), level_over(tile_of(f.bottom)) });

        bool any = false;
        std::vector<save::model::WaterBody> settled;
        std::vector<bool> held(grid.cells.size());
        std::vector<bool> covered(grid.cells.size());
        for (save::model::WaterBody& lake : water.bodies)
        {
            // Touched: on a tile that moved, or beside one that now dips below its level.
            std::ranges::fill(held, false);
            for (const save::model::WaterTri& t : lake.tris)
                held[at(t.col, t.row)] = true;
            bool touched = false;
            float floor = lake.level;
            for (const save::model::WaterTri& t : lake.tris)
            {
                floor = std::min(floor, grid.at(t.col, t.row).min_height());
                touched = touched || moved[at(t.col, t.row)];
                for (const auto& [dc, dr] : { std::pair { 1, 0 }, std::pair { -1, 0 }, std::pair { 0, 1 }, std::pair { 0, -1 } })
                {
                    if (grid.contains(t.col + dc, t.row + dr) && moved[at(t.col + dc, t.row + dr)] && grid.at(t.col + dc, t.row + dr).min_height() < lake.level)
                        touched = true;
                }
            }
            if (!touched)
            {
                settled.push_back(std::move(lake));
                continue;
            }
            // Its water spreads over the ground that moved, but not onto ground that was dry and did not move:
            // from a tile it held it stops there as before, from one that moved it runs out.
            const auto crossing = [&](save::model::Tile from, save::model::Tile to) {
                if (held[at(to.col, to.row)] || moved[at(to.col, to.row)])
                    return Crossing::Flows;
                return moved[at(from.col, from.row)] ? Crossing::Leaks : Crossing::Stops;
            };
            // The highest whole level from its own down that runs out nowhere, in as many parts as the ground
            // now splits it into.
            std::vector<save::model::WaterBody> parts;
            for (float level = lake.level; level > floor; level = std::ceil(level) - 1)
            {
                parts.clear();
                std::ranges::fill(covered, false);
                bool leaks = false;
                for (const save::model::WaterTri& t : lake.tris)
                {
                    if (covered[at(t.col, t.row)] || grid.at(t.col, t.row).min_height() >= level)
                        continue;
                    auto part = flood(grid, { t.col, t.row }, level, crossing);
                    if (!part)
                    {
                        leaks = true;
                        break;
                    }
                    for (const save::model::WaterTri& p : part->tris)
                        covered[at(p.col, p.row)] = true;
                    covered[at(t.col, t.row)] = true;
                    if (!part->tris.empty())
                        parts.push_back(std::move(*part));
                }
                if (!leaks)
                    break;
                parts.clear();
            }
            const auto same = [](const save::model::WaterTri& a, const save::model::WaterTri& b) { return a.col == b.col && a.row == b.row && a.triangle == b.triangle && a.mask == b.mask; };
            any = any || parts.size() != 1 || parts[0].level != lake.level || !std::ranges::is_permutation(parts[0].tris, lake.tris, same);
            for (save::model::WaterBody& part : parts)
                settled.push_back(std::move(part));
        }
        water.bodies = std::move(settled);

        // A waterfall goes when the ground at it moved, or the water at either end is gone or at another level.
        const auto beside = [&](const save::model::WorldPoint3& p) {
            const save::model::Tile t = tile_of(p);
            for (int dr = -1; dr <= 1; ++dr)
                for (int dc = -1; dc <= 1; ++dc)
                {
                    if (grid.contains(t.col + dc, t.row + dr) && moved[at(t.col + dc, t.row + dr)])
                        return true;
                }
            return false;
        };
        std::size_t next = 0;
        const std::size_t falls_before = falls.size();
        std::erase_if(falls, [&](const save::model::Waterfall& f) {
            const auto& [top, bottom] = ends[next++];
            return beside(f.lip_left) || beside(f.lip_right) || beside(f.top) || beside(f.bottom) || level_over(tile_of(f.top)) != top || level_over(tile_of(f.bottom)) != bottom;
        });
        return any || falls.size() != falls_before;
    }

    bool holds(const save::model::WaterBody& body, save::model::Tile tile)
    {
        const auto [col, row] = tile;
        return std::ranges::any_of(body.tris, [&](const save::model::WaterTri& t) { return t.col == col && t.row == row; });
    }

    bool holds_water(std::span<const save::model::WaterBody> bodies, save::model::Tile tile)
    {
        return std::ranges::any_of(bodies, [&](const save::model::WaterBody& b) { return holds(b, tile); });
    }

    std::optional<std::int32_t> water_surface(std::span<const save::model::WaterBody> bodies, save::model::Tile tile)
    {
        const auto lake = std::ranges::find_if(bodies, [&](const save::model::WaterBody& b) { return holds(b, tile); });
        if (lake == bodies.end())
            return std::nullopt;
        return static_cast<std::int32_t>(std::ceil(lake->level));
    }

} // namespace rct3::park::water
