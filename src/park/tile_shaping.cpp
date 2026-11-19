// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/tile_shaping.hpp"

#include "core/types.hpp"
#include "park/view_lines.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

namespace rct3::park
{

    namespace
    {
        constexpr units::Distance kMarkWidth { 0.15f }; ///< Across the marks.
        constexpr f32 kCrossReach = 0.6f;               ///< Of a corner's cross, from the corner along each edge.

        bool inside(const TileStroke& s, i32 col, i32 row)
        {
            return col >= s.col0 && col <= s.col1 && row >= s.row0 && row <= s.row1;
        }

        /// Corner `k` of tile (`col`, `row`) as the corner point it sits on.
        glm::ivec2 point_of(i32 col, i32 row, usize k)
        {
            return { col + static_cast<i32>(k & 1), row + static_cast<i32>(k >> 1) };
        }

        /// Of the heights `candidates` hold, the one nearest `wanted`, if within kSnapReach; else `wanted`.
        f32 nearest(f32 wanted, std::span<const f32> candidates)
        {
            f32 best = wanted;
            f32 gap = kSnapReach;
            for (const f32 h : candidates)
            {
                if (std::abs(h - wanted) <= gap)
                {
                    gap = std::abs(h - wanted);
                    best = h;
                }
            }
            return best;
        }

        glm::vec3 corner_at(const save::model::TerrainGrid& grid, i32 col, i32 row, usize k)
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
        const auto side = [](f32 f) { return f < kPartEdge ? -1 : f > 1 - kPartEdge ? 1 : 0; };
        const i32 x = side(within.x);
        const i32 z = side(within.y);
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

    TileStroke begin_tile_stroke(const save::model::TerrainGrid& grid, i32 col0, i32 row0, i32 col1, i32 row1, TilePart part)
    {
        TileStroke s { std::max(col0, 0), std::max(row0, 0), std::min(col1, grid.width - 1), std::min(row1, grid.height - 1), part, { }, 0, 0 };
        s.low = std::numeric_limits<f32>::infinity();
        s.high = -s.low;
        const auto picked = part_corners(part);
        for (i32 r = s.row0; r <= s.row1; ++r)
        {
            for (i32 c = s.col0; c <= s.col1; ++c)
            {
                s.before.push_back(grid.at(c, r).corners);
                for (usize k = 0; k < 4; ++k)
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

    f32 shape_tiles(save::model::TerrainGrid& grid, const TileStroke& stroke, TileShaping shaping, f32 amount, const std::function<bool(save::model::Tile)>& free)
    {
        const auto picked = part_corners(stroke.part);
        const bool up = amount >= 0;
        const f32 target = (up ? stroke.low : stroke.high) + amount;
        const i32 columns = stroke.col1 - stroke.col0 + 1;
        f32 moved = 0;
        for (i32 r = stroke.row0; r <= stroke.row1; ++r)
        {
            for (i32 c = stroke.col0; c <= stroke.col1; ++c)
            {
                const auto& before = stroke.before[static_cast<usize>((r - stroke.row0) * columns + (c - stroke.col0))];
                // A tile refused part of the way through a stroke stays where it had got to.
                std::array<f32, 4> corners = grid.at(c, r).corners;
                if (free({ c, r }))
                {
                    corners = before;
                    for (usize k = 0; k < 4; ++k)
                    {
                        // Only corners the drag passes move: the others are already further that way.
                        if (!picked[k] || (up ? before[k] >= target : before[k] <= target))
                            continue;
                        f32 h = target;
                        if (shaping == TileShaping::Whole)
                            h = std::round(h);
                        else if (shaping == TileShaping::Snap)
                        {
                            // The heights the tiles round this point outside the stroke have, and its own.
                            std::vector<f32> candidates { before[k] };
                            const glm::ivec2 p = point_of(c, r, k);
                            for (i32 dr = 0; dr < 2; ++dr)
                            {
                                for (i32 dc = 0; dc < 2; ++dc)
                                {
                                    const i32 tc = p.x - 1 + dc;
                                    const i32 tr = p.y - 1 + dr;
                                    if (grid.contains(tc, tr) && !inside(stroke, tc, tr))
                                        candidates.push_back(grid.at(tc, tr).corners[static_cast<usize>((1 - dc) + 2 * (1 - dr))]);
                                }
                            }
                            h = nearest(h, candidates);
                        }
                        corners[k] = h;
                    }
                }
                grid.at(c, r).corners = corners;
                for (usize k = 0; k < 4; ++k)
                    moved += std::abs(corners[k] - before[k]);
            }
        }
        return moved;
    }

    void tile_marks(TerrainMesh& mesh, const save::model::TerrainGrid& grid, i32 col0, i32 row0, i32 col1, i32 row1, TilePart part, glm::vec4 colour)
    {
        col0 = std::max(col0, 0);
        row0 = std::max(row0, 0);
        col1 = std::min(col1, grid.width - 1);
        row1 = std::min(row1, grid.height - 1);
        const auto picked = part_corners(part);
        const auto count = std::ranges::count(picked, true);
        for (i32 r = row0; r <= row1; ++r)
        {
            for (i32 c = col0; c <= col1; ++c)
            {
                const auto at = [&](usize k) { return corner_at(grid, c, r, k); };
                if (count == 1)
                {
                    // A cross on the corner, along the tile's two edges from it.
                    const auto k = static_cast<usize>(std::ranges::find(picked, true) - picked.begin());
                    const glm::vec3 p = at(k);
                    for (const usize other : { k ^ 1u, k ^ 2u })
                        ground_strip(mesh, p, p + (at(other) - p) * (kCrossReach / grid.tile_size_x), colour, kMarkWidth.value);
                    continue;
                }
                // The tile's edges whose both ends are picked.
                for (const auto& [a, b] : { std::pair { 0u, 1u }, std::pair { 1u, 3u }, std::pair { 3u, 2u }, std::pair { 2u, 0u } })
                {
                    if (picked[a] && picked[b])
                        ground_strip(mesh, at(a), at(b), colour, kMarkWidth.value);
                }
            }
        }
    }

} // namespace rct3::park
