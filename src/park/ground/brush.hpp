// SPDX-License-Identifier: GPL-3.0-or-later
//
// The land brush: a radius in world units round the point of the ground under the pointer, the same for
// shaping, smoothing and painting. Painting covers the tile holding the point and every tile with a corner
// within the radius. The tools that work on a square of whole tiles (tile shaping, land buying) take one as
// many tiles across as the brush is wide, round the pointer's tile; even counts reach one tile further to the
// right and down than to the left and up.
#pragma once

#include "save/model/world.hpp"

#include <glm/vec2.hpp>
#include <vector>

namespace rct3::park::ground
{

    inline constexpr int kLargestBrush = 64; ///< Radius, in world units
    inline constexpr int kDefaultBrush = 10;

    /// How many tiles across a brush of `radius` is, on tiles `tile_size` wide: one at least.
    [[nodiscard]] int brush_across(int radius, float tile_size);

    /// The square `size` tiles across round `middle`, those inside a grid `columns` by `rows` only.
    [[nodiscard]] std::vector<save::model::Tile> brush_tiles(save::model::Tile middle, int size, int columns, int rows);

    /// The tiles a round brush covers: the one holding `point` and those with a corner within `reach` of it,
    /// both in tile units, inside a grid `columns` by `rows` only.
    [[nodiscard]] std::vector<save::model::Tile> round_brush_tiles(glm::vec2 point, float reach, int columns, int rows);

} // namespace rct3::park::ground
