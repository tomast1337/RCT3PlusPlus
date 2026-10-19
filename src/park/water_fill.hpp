// SPDX-License-Identifier: GPL-3.0-or-later
//
// Filling a lake. Water poured on a tile up to a level spreads like a flood: every tile with a corner below
// the level that can be reached from it, crossing from tile to tile where both sides of the shared edge have a
// corner below the level. Each such tile holds water in its triangles that dip below the level, as the saved
// water bodies keep it.
#pragma once

#include "save/model/world.hpp"

#include <functional>
#include <optional>

namespace rct3::park
{

    /// The water body filled from tile `col`, `row` to `level`. Its triangles are empty when the tile has no
    /// corner below the level. Nothing when the flood reaches a tile `allowed` refuses.
    [[nodiscard]] std::optional<save::WaterBody> fill_water(const save::TerrainGrid& grid, int col, int row, float level, const std::function<bool(int col, int row)>& allowed);

    /// Whether `body` holds water on tile `col`, `row`.
    [[nodiscard]] bool holds(const save::WaterBody& body, int col, int row);

} // namespace rct3::park
