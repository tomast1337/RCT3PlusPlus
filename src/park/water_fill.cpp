// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/water_fill.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
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

    bool holds(const save::WaterBody& body, save::Tile tile)
    {
        const auto [col, row] = tile;
        return std::ranges::any_of(body.tris, [&](const save::WaterTri& t) { return t.col == col && t.row == row; });
    }

    bool holds_water(std::span<const save::WaterBody> bodies, save::Tile tile)
    {
        return std::ranges::any_of(bodies, [&](const save::WaterBody& b) { return holds(b, tile); });
    }

} // namespace rct3::park
