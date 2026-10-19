// SPDX-License-Identifier: GPL-3.0-or-later
//
// Smoothing the ground under a brush, a little more for every moment the brush is held: our own tools.
//
// Most work on the tiles' shared corner points (park/ground_shapes.hpp), each moved part of the way to where
// the tool wants it, the share growing with `strength` (the part of the way a pass goes, 0 to 1). Points the
// owner holds and cliff edges stay.
//   - Average eases each point towards the mean of the four next to it.
//   - Random roughens: each point goes up or down by up to half a unit.
//   - Level goes towards `target` (the owner's: the height under the pointer, the brush's mean, a whole unit).
//   - Whole units goes towards each point's nearest whole unit, for rides and scenery to stand on.
// The other two work on whole tiles that the owner lets them change, easing them part of the way each pass too:
//   - Join edges takes away the cliffs between those tiles: each corner the tiles round it disagree on goes
//     towards their mean.
//   - Tile edges does the opposite: each tile goes towards flat at its mean height, to the nearest whole unit,
//     so the edges between tiles show.
#pragma once

#include "save/model/world.hpp"

#include <cstdint>
#include <functional>
#include <glm/vec2.hpp>

namespace rct3::park
{

    enum class GroundSmoothing : std::uint8_t
    {
        Average,
        Random,
        Level,
        WholeUnits,
        JoinEdges,
        TileEdges,
    };

    /// The mean height of the corner points within `reach` tiles of `middle`; 0 when there are none.
    [[nodiscard]] float mean_height(const save::TerrainGrid& grid, glm::vec2 middle, float reach);

    /// One pass of `tool` over the ground within `reach` tiles of `middle`, on tiles `free` lets change. `seed`
    /// picks Random's ups and downs. Returns the sum of how far the corners moved.
    float smooth_ground(save::TerrainGrid& grid, GroundSmoothing tool, glm::vec2 middle, float reach, float strength, float target, std::uint64_t seed, const std::function<bool(int col, int row)>& free);

} // namespace rct3::park
