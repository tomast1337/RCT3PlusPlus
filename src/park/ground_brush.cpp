// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_brush.hpp"

#include <algorithm>

namespace rct3::park
{

    std::vector<save::Tile> brush_tiles(save::Tile middle, int size, bool round, int columns, int rows)
    {
        size = std::clamp(size, 1, kLargestBrush);
        const int first = -(size - 1) / 2;
        const float reach = static_cast<float>(size) / 2;
        std::vector<save::Tile> tiles;
        for (int dr = first; dr < first + size; ++dr)
            for (int dc = first; dc < first + size; ++dc)
            {
                // An even size has no middle tile: measured from the corner the four in the middle share.
                const float half = size % 2 == 0 ? 0.5f : 0.0f;
                const float x = static_cast<float>(dc) - half;
                const float y = static_cast<float>(dr) - half;
                if (round && x * x + y * y > reach * reach)
                    continue;
                const save::Tile t { middle.col + dc, middle.row + dr };
                if (t.col >= 0 && t.row >= 0 && t.col < columns && t.row < rows)
                    tiles.push_back(t);
            }
        return tiles;
    }

} // namespace rct3::park
