// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground/brush.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/geometric.hpp>

namespace rct3::park::ground
{

    i32 brush_across(i32 radius, f32 tile_size)
    {
        return std::max(static_cast<i32>(std::lround(2 * static_cast<f32>(radius) / tile_size)), 1);
    }

    std::vector<save::model::Tile> brush_tiles(save::model::Tile middle, i32 size, i32 columns, i32 rows)
    {
        size = std::max(size, 1);
        const i32 first = -(size - 1) / 2;
        std::vector<save::model::Tile> tiles;
        for (i32 r = std::max(middle.row + first, 0); r < std::min(middle.row + first + size, rows); ++r)
            for (i32 c = std::max(middle.col + first, 0); c < std::min(middle.col + first + size, columns); ++c)
                tiles.push_back({ c, r });
        return tiles;
    }

    std::vector<save::model::Tile> round_brush_tiles(glm::vec2 point, f32 reach, i32 columns, i32 rows)
    {
        const save::model::Tile under { static_cast<i32>(std::floor(point.x)), static_cast<i32>(std::floor(point.y)) };
        std::vector<save::model::Tile> tiles;
        for (i32 r = std::max(static_cast<i32>(std::floor(point.y - reach)) - 1, 0); r < std::min(static_cast<i32>(std::ceil(point.y + reach)) + 1, rows); ++r)
            for (i32 c = std::max(static_cast<i32>(std::floor(point.x - reach)) - 1, 0); c < std::min(static_cast<i32>(std::ceil(point.x + reach)) + 1, columns); ++c)
            {
                bool within = c == under.col && r == under.row;
                for (i32 k = 0; k < 4 && !within; ++k)
                {
                    const glm::vec2 corner { static_cast<f32>(c + k % 2), static_cast<f32>(r + k / 2) };
                    within = glm::dot(corner - point, corner - point) <= reach * reach;
                }
                if (within)
                    tiles.push_back({ c, r });
            }
        return tiles;
    }

} // namespace rct3::park::ground
