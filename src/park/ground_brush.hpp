// SPDX-License-Identifier: GPL-3.0-or-later
//
// The tiles a land brush covers round the tile under the pointer: a square `size` tiles across, or a round
// one, the tiles whose middles lie within half of `size` of the middle tile's. Even sizes reach one tile
// further to the right and down than to the left and up.
#pragma once

#include "park/footpath_builder.hpp"

#include <vector>

namespace rct3::park
{

    inline constexpr int kLargestBrush = 16; ///< Tiles across

    /// The tiles covered round `middle`, those inside a grid `columns` by `rows` only.
    [[nodiscard]] std::vector<PathSpot> brush_tiles(PathSpot middle, int size, bool round, int columns, int rows);

} // namespace rct3::park
