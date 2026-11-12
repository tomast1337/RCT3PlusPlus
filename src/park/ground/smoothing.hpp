// SPDX-License-Identifier: GPL-3.0-or-later
//
// Smoothing the ground under a brush.
//
// Each tool knows where it wants every corner (below). A pass moves the corners towards there, strongest at
// the brush's middle and falling off with the square of the distance to nothing at its edge. How far is the
// owner's, in one of two ways:
//   - sprayed, a little every moment the button is held: all the way, but no corner further than `most` (at
//     the middle), so the ground moves at an even speed set by the spray speed;
//   - dragged, from the ground as the stroke found it: `share` of the way at the middle, growing with how far
//     the pointer was dragged, with no limit.
// Most tools work on the tiles' corner points. Every tile keeps its own corners: where the tiles round a point
// disagree, they move together and the cliff between them stays as high as it was. Points the owner holds stay.
//   - Average eases each point towards the mean of the four next to it, each side of a cliff by itself.
//   - Random roughens: each point goes up or down by up to half a unit.
//   - Level goes towards `target`, one height for the whole brush (the owner's: the height under the pointer, or
//     that height's whole unit, which makes a flat pad for rides and scenery to stand on).
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

namespace rct3::park::ground
{

    enum class GroundSmoothing : std::uint8_t
    {
        Average,
        Random,
        Level,
        JoinEdges,
        TileEdges,
    };

    /// The whole unit a pad levelled from `height` lies at.
    [[nodiscard]] float whole_level(float height);

    /// The mean height of the corner points within `reach` tiles of `middle`; 0 when there are none.
    [[nodiscard]] float mean_height(const save::model::TerrainGrid& grid, glm::vec2 middle, float reach);

    inline constexpr float kSmoothSprayShare = 0.5f; ///< Of a raising spray's speed, what a smoothing spray moves the ground at
    inline constexpr float kSmoothDragRate = 0.01f;  ///< Of the way to the smoothed ground, for each pixel dragged past the dead zone

    /// One pass of `tool` over the ground within `reach` tiles of `middle`, on tiles `free` lets change: at the
    /// middle `share` of the way (all of it from 1), no corner further than `most` there. `seed` picks Random's
    /// ups and downs. Returns the sum of how far the corners moved.
    float smooth_ground(save::model::TerrainGrid& grid, GroundSmoothing tool, glm::vec2 middle, float reach, float share, float most, float target, std::uint64_t seed, const std::function<bool(save::model::Tile)>& free);

} // namespace rct3::park::ground
