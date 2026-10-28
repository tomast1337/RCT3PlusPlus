// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/water_fill.hpp"

#include "park/waterfall_making.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rct3::park
{

    namespace
    {
        bool below(const save::TerrainCell& cell, std::size_t a, std::size_t b, float level)
        {
            return cell.corners[a] < level || cell.corners[b] < level;
        }
    } // namespace

    std::optional<save::WaterBody> fill_water(const save::TerrainGrid& grid, save::Tile tile, float level, const std::function<bool(save::Tile)>& allowed)
    {
        const auto [col, row] = tile;
        save::WaterBody body;
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
            if (!allowed({ c, r }))
                return std::nullopt;
            for (int t = 0; t < 2; ++t)
            {
                if (const auto mask = save::expected_water_mask(grid, level, { c, r }, t); mask && *mask != 0)
                    body.tris.push_back({ static_cast<std::uint8_t>(c), static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(t), *mask });
            }
            const save::TerrainCell& cell = grid.at(c, r);
            for (const Way& w : kWays)
            {
                const int nc = c + w.dc;
                const int nr = r + w.dr;
                if (!grid.contains(nc, nr) || seen[static_cast<std::size_t>(nr * grid.width + nc)])
                    continue;
                if (below(cell, w.a, w.b, level) && below(grid.at(nc, nr), w.far_a, w.far_b, level))
                {
                    seen[static_cast<std::size_t>(nr * grid.width + nc)] = true;
                    open.emplace_back(nc, nr);
                }
            }
        }
        return body;
    }

    std::optional<save::WaterLayer> pour_water(const save::WaterLayer& water, const save::TerrainGrid& grid, save::Tile tile, float level, const std::function<bool(save::Tile)>& allowed)
    {
        const auto lake = fill_water(grid, tile, std::ceil(level), allowed);
        if (!lake || lake->tris.empty())
            return lake ? std::optional { water } : std::nullopt;
        const auto reached = [&](const save::WaterBody& b) { return std::ranges::any_of(lake->tris, [&](const save::WaterTri& t) { return holds(b, { t.col, t.row }); }); };
        if (std::ranges::any_of(water.bodies, [&](const save::WaterBody& b) { return b.level > lake->level && reached(b); }))
            return std::nullopt;
        save::WaterLayer poured = water;
        std::erase_if(poured.bodies, reached);
        poured.bodies.push_back(*lake);
        return poured;
    }

    bool remove_lake(save::WaterLayer& water, std::vector<save::Waterfall>& falls, save::TerrainGrid& grid, save::Tile tile)
    {
        const auto lake = std::ranges::find_if(water.bodies, [&](const save::WaterBody& b) { return holds(b, tile); });
        if (lake == water.bodies.end())
            return false;
        const auto on_lake = [&](const save::WorldPoint3& p) { return holds(*lake, { static_cast<int>(std::floor((p.x - grid.origin_x) / grid.tile_size_x)), static_cast<int>(std::floor((p.z - grid.origin_z) / grid.tile_size_z)) }); };
        std::erase_if(falls, [&](const save::Waterfall& f) {
            if (!on_lake(f.top) && !on_lake(f.bottom))
                return false;
            restore_spillway(grid, f);
            return true;
        });
        water.bodies.erase(lake);
        return true;
    }

    bool drain_moved(save::WaterLayer& water, std::vector<save::Waterfall>& falls, const save::TerrainGrid& was, const save::TerrainGrid& grid)
    {
        const auto key = [](int col, int row) { return static_cast<std::uint32_t>(row) << 16 | static_cast<std::uint16_t>(col); };
        std::unordered_set<std::uint32_t> moved;
        for (int r = 0; r < grid.height; ++r)
            for (int c = 0; c < grid.width; ++c)
            {
                if (was.contains(c, r) && was.at(c, r).corners != grid.at(c, r).corners)
                    moved.insert(key(c, r));
            }
        if (moved.empty())
            return false;
        std::unordered_map<std::uint32_t, std::size_t> lake_of;
        for (std::size_t i = 0; i < water.bodies.size(); ++i)
            for (const save::WaterTri& t : water.bodies[i].tris)
                lake_of.emplace(key(t.col, t.row), i);
        std::vector<bool> gone(water.bodies.size(), false);
        const auto take = [&](int col, int row, std::optional<float> under) {
            const auto lake = lake_of.find(key(col, row));
            if (lake != lake_of.end() && (!under || *under < water.bodies[lake->second].level))
                gone[lake->second] = true;
        };
        for (const std::uint32_t k : moved)
        {
            const int col = static_cast<int>(k & 0xFFFF);
            const int row = static_cast<int>(k >> 16);
            take(col, row, std::nullopt);
            for (const auto [dc, dr] : { std::pair { 1, 0 }, std::pair { -1, 0 }, std::pair { 0, 1 }, std::pair { 0, -1 } })
                take(col + dc, row + dr, grid.at(col, row).min_height());
        }
        const auto tile_of = [&](const save::WorldPoint3& p) { return save::Tile { static_cast<int>(std::floor((p.x - grid.origin_x) / grid.tile_size_x)), static_cast<int>(std::floor((p.z - grid.origin_z) / grid.tile_size_z)) }; };
        const auto beside = [&](const save::WorldPoint3& p) {
            const save::Tile t = tile_of(p);
            for (int dr = -1; dr <= 1; ++dr)
                for (int dc = -1; dc <= 1; ++dc)
                {
                    if (moved.contains(key(t.col + dc, t.row + dr)))
                        return true;
                }
            return false;
        };
        for (const save::Waterfall& f : falls)
        {
            if (!beside(f.lip_left) && !beside(f.lip_right) && !beside(f.top) && !beside(f.bottom))
                continue;
            for (const save::WorldPoint3& end : { f.top, f.bottom })
                take(tile_of(end).col, tile_of(end).row, std::nullopt);
        }
        const auto on_gone = [&](const save::WorldPoint3& p) {
            const auto lake = lake_of.find(key(tile_of(p).col, tile_of(p).row));
            return lake != lake_of.end() && gone[lake->second];
        };
        const std::size_t falls_before = falls.size();
        std::erase_if(falls, [&](const save::Waterfall& f) { return on_gone(f.top) || on_gone(f.bottom) || beside(f.lip_left) || beside(f.lip_right) || beside(f.top) || beside(f.bottom); });
        bool any = falls.size() != falls_before;
        for (std::size_t i = water.bodies.size(); i-- > 0;)
        {
            if (!gone[i])
                continue;
            water.bodies.erase(water.bodies.begin() + static_cast<std::ptrdiff_t>(i));
            any = true;
        }
        return any;
    }

    bool holds(const save::WaterBody& body, save::Tile tile)
    {
        const auto [col, row] = tile;
        return std::ranges::any_of(body.tris, [&](const save::WaterTri& t) { return t.col == col && t.row == row; });
    }

    bool holds_water(std::span<const save::WaterBody> bodies, save::Tile tile)
    {
        return std::ranges::any_of(bodies, [&](const save::WaterBody& b) { return holds(b, tile); });
    }

    std::optional<std::int32_t> water_surface(std::span<const save::WaterBody> bodies, save::Tile tile)
    {
        const auto lake = std::ranges::find_if(bodies, [&](const save::WaterBody& b) { return holds(b, tile); });
        if (lake == bodies.end())
            return std::nullopt;
        return static_cast<std::int32_t>(std::ceil(lake->level));
    }

} // namespace rct3::park
