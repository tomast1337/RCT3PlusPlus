// SPDX-License-Identifier: GPL-3.0-or-later
//
// Arrows shown while building, so the player sees which way a thing goes before it is placed.
//
// A stall being placed shows the path tool's blue arrow lying flat on the ground, one tile square, across the
// edge of the stall's tile on the side it serves from: half over the stall's own tile and half over the tile
// beside it, the arrow's head on the outer half pointing away from the stall, to where its path goes. It
// stands at the stall's height and does not blink.
//
// Track being built shows two thin arrows a little above the rails. Where the next piece starts, a white open
// arrow points along the track; where it ends, a closed triangle points on, green when the track is built on
// at its far end and red when it is built back from its start.
#pragma once

#include "park/terrain_mesh.hpp"
#include "park/track_pieces.hpp"
#include "save/model/world.hpp"

#include <cstdint>

namespace rct3::park
{

    /// The stall's arrow: on tile (`col`, `row`) of `grid` at `height`, facing `facing` (0 lower rows, 1 lower
    /// columns, 2 higher rows, 3 higher columns). Its picture's bottom edge is the arrow's head. One batch.
    [[nodiscard]] TerrainMesh facing_arrow(std::int32_t col, std::int32_t row, float height, std::uint8_t facing, const save::TerrainGrid& grid);

    /// The track's arrows: the open one at `start`, the triangle at `end`, green when `forward` else red. Thin
    /// flat strips in their vertex colours, drawn with no picture. One batch.
    [[nodiscard]] TerrainMesh track_arrows(const TrackTip& start, const TrackTip& end, bool forward);

} // namespace rct3::park
