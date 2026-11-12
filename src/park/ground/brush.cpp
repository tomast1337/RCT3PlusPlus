// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground/brush.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace rct3::park::ground
{

    int brush_across(int radius, float tile_size)
    {
        return std::max(static_cast<int>(std::lround(2 * static_cast<float>(radius) / tile_size)), 1);
    }

    std::vector<save::model::Tile> brush_tiles(save::model::Tile middle, int size, int columns, int rows)
    {
        size = std::max(size, 1);
        const int first = -(size - 1) / 2;
        std::vector<save::model::Tile> tiles;
        for (int r = std::max(middle.row + first, 0); r < std::min(middle.row + first + size, rows); ++r)
            for (int c = std::max(middle.col + first, 0); c < std::min(middle.col + first + size, columns); ++c)
                tiles.push_back({ c, r });
        return tiles;
    }

    std::vector<save::model::Tile> round_brush_tiles(glm::vec2 point, float reach, int columns, int rows)
    {
        const save::model::Tile under { static_cast<int>(std::floor(point.x)), static_cast<int>(std::floor(point.y)) };
        std::vector<save::model::Tile> tiles;
        for (int r = std::max(static_cast<int>(std::floor(point.y - reach)) - 1, 0); r < std::min(static_cast<int>(std::ceil(point.y + reach)) + 1, rows); ++r)
            for (int c = std::max(static_cast<int>(std::floor(point.x - reach)) - 1, 0); c < std::min(static_cast<int>(std::ceil(point.x + reach)) + 1, columns); ++c)
            {
                bool within = c == under.col && r == under.row;
                for (int k = 0; k < 4 && !within; ++k)
                {
                    const glm::vec2 corner { static_cast<float>(c + k % 2), static_cast<float>(r + k / 2) };
                    within = glm::dot(corner - point, corner - point) <= reach * reach;
                }
                if (within)
                    tiles.push_back({ c, r });
            }
        return tiles;
    }

} // namespace rct3::park::ground
