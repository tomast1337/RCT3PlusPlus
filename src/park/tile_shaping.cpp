// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/tile_shaping.hpp"

#include "park/view_lines.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

namespace rct3::park
{

    namespace
    {
        constexpr float kMarkWidth = 0.15f; ///< Across the marks, in world units.
        constexpr float kCrossReach = 0.6f; ///< Of a corner's cross, from the corner along each edge.

        bool inside(const TileStroke& s, int col, int row)
        {
            return col >= s.col0 && col <= s.col1 && row >= s.row0 && row <= s.row1;
        }

        /// Corner `k` of tile (`col`, `row`) as the corner point it sits on.
        glm::ivec2 point_of(int col, int row, std::size_t k)
        {
            return { col + static_cast<int>(k & 1), row + static_cast<int>(k >> 1) };
        }

        /// Of the heights `candidates` hold, the one nearest `wanted`, if within kSnapReach; else `wanted`.
        float nearest(float wanted, std::span<const float> candidates)
        {
            float best = wanted;
            float gap = kSnapReach;
            for (const float h : candidates)
            {
                if (std::abs(h - wanted) <= gap)
                {
                    gap = std::abs(h - wanted);
                    best = h;
                }
            }
            return best;
        }

        glm::vec3 corner_at(const save::TerrainGrid& grid, int col, int row, std::size_t k)
        {
            const glm::ivec2 p = point_of(col, row, k);
            return { grid.world_x(p.x), grid.at(col, row).corners[k], grid.world_z(p.y) };
        }
    } // namespace

    std::array<bool, 4> part_corners(TilePart part)
    {
        switch (part)
        {
            case TilePart::Whole:
                return { true, true, true, true };
            case TilePart::Left:
                return { true, false, true, false };
            case TilePart::Right:
                return { false, true, false, true };
            case TilePart::Near:
                return { true, true, false, false };
            case TilePart::Far:
                return { false, false, true, true };
            case TilePart::NearLeft:
                return { true, false, false, false };
            case TilePart::NearRight:
                return { false, true, false, false };
            case TilePart::FarLeft:
                return { false, false, true, false };
            case TilePart::FarRight:
                return { false, false, false, true };
        }
        return { };
    }

    TilePart part_at(glm::vec2 within)
    {
        // -1 low edge, 0 middle, 1 high edge, along each axis.
        const auto side = [](float f) { return f < kPartEdge ? -1 : f > 1 - kPartEdge ? 1 : 0; };
        const int x = side(within.x);
        const int z = side(within.y);
        if (x == 0 && z == 0)
            return TilePart::Whole;
        if (z == 0)
            return x < 0 ? TilePart::Left : TilePart::Right;
        if (x == 0)
            return z < 0 ? TilePart::Near : TilePart::Far;
        if (z < 0)
            return x < 0 ? TilePart::NearLeft : TilePart::NearRight;
        return x < 0 ? TilePart::FarLeft : TilePart::FarRight;
    }

    TileStroke begin_tile_stroke(const save::TerrainGrid& grid, int col0, int row0, int col1, int row1, TilePart part)
    {
        TileStroke s { std::max(col0, 0), std::max(row0, 0), std::min(col1, grid.width - 1), std::min(row1, grid.height - 1), part, { }, 0, 0 };
        s.low = std::numeric_limits<float>::infinity();
        s.high = -s.low;
        const auto picked = part_corners(part);
        for (int r = s.row0; r <= s.row1; ++r)
        {
            for (int c = s.col0; c <= s.col1; ++c)
            {
                s.before.push_back(grid.at(c, r).corners);
                for (std::size_t k = 0; k < 4; ++k)
                {
                    if (!picked[k])
                        continue;
                    s.low = std::min(s.low, s.before.back()[k]);
                    s.high = std::max(s.high, s.before.back()[k]);
                }
            }
        }
        if (s.before.empty())
            s.low = s.high = 0;
        return s;
    }

    float shape_tiles(save::TerrainGrid& grid, const TileStroke& stroke, TileShaping shaping, float amount, const std::function<bool(int col, int row)>& free)
    {
        const auto picked = part_corners(stroke.part);
        const bool up = amount >= 0;
        const float target = (up ? stroke.low : stroke.high) + amount;
        const int columns = stroke.col1 - stroke.col0 + 1;
        float moved = 0;
        for (int r = stroke.row0; r <= stroke.row1; ++r)
        {
            for (int c = stroke.col0; c <= stroke.col1; ++c)
            {
                const auto& before = stroke.before[static_cast<std::size_t>((r - stroke.row0) * columns + (c - stroke.col0))];
                std::array<float, 4> corners = before;
                if (free(c, r))
                {
                    for (std::size_t k = 0; k < 4; ++k)
                    {
                        // Only corners the drag passes move: the others are already further that way.
                        if (!picked[k] || (up ? before[k] >= target : before[k] <= target))
                            continue;
                        float h = target;
                        if (shaping == TileShaping::Whole)
                            h = std::round(h);
                        else if (shaping == TileShaping::Snap)
                        {
                            // The heights the tiles round this point outside the stroke have, and its own.
                            std::vector<float> candidates { before[k] };
                            const glm::ivec2 p = point_of(c, r, k);
                            for (int dr = 0; dr < 2; ++dr)
                            {
                                for (int dc = 0; dc < 2; ++dc)
                                {
                                    const int tc = p.x - 1 + dc;
                                    const int tr = p.y - 1 + dr;
                                    if (grid.contains(tc, tr) && !inside(stroke, tc, tr))
                                        candidates.push_back(grid.at(tc, tr).corners[static_cast<std::size_t>((1 - dc) + 2 * (1 - dr))]);
                                }
                            }
                            h = nearest(h, candidates);
                        }
                        corners[k] = h;
                    }
                }
                grid.at(c, r).corners = corners;
                for (std::size_t k = 0; k < 4; ++k)
                    moved += std::abs(corners[k] - before[k]);
            }
        }
        return moved;
    }

    void tile_marks(TerrainMesh& mesh, const save::TerrainGrid& grid, int col0, int row0, int col1, int row1, TilePart part, glm::vec4 colour)
    {
        col0 = std::max(col0, 0);
        row0 = std::max(row0, 0);
        col1 = std::min(col1, grid.width - 1);
        row1 = std::min(row1, grid.height - 1);
        const auto picked = part_corners(part);
        const auto count = std::ranges::count(picked, true);
        for (int r = row0; r <= row1; ++r)
        {
            for (int c = col0; c <= col1; ++c)
            {
                const auto at = [&](std::size_t k) { return corner_at(grid, c, r, k); };
                if (count == 1)
                {
                    // A cross on the corner, along the tile's two edges from it.
                    const auto k = static_cast<std::size_t>(std::ranges::find(picked, true) - picked.begin());
                    const glm::vec3 p = at(k);
                    for (const std::size_t other : { k ^ 1u, k ^ 2u })
                        ground_strip(mesh, p, p + (at(other) - p) * (kCrossReach / grid.tile_size_x), colour, kMarkWidth);
                    continue;
                }
                // The tile's edges whose both ends are picked.
                for (const auto& [a, b] : { std::pair { 0u, 1u }, std::pair { 1u, 3u }, std::pair { 3u, 2u }, std::pair { 2u, 0u } })
                {
                    if (picked[a] && picked[b])
                        ground_strip(mesh, at(a), at(b), colour, kMarkWidth);
                }
            }
        }
    }

} // namespace rct3::park
