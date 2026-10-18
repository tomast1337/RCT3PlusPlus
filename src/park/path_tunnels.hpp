// SPDX-License-Identifier: GPL-3.0-or-later
//
// The room tunnel paths take in the ground. Each tunnel piece (footpath_builder.hpp) hollows out its tile from
// just under its lowest point to kTunnelHeight over its highest. The ground's walls along that tile's edges
// leave the hollow open, so a tunnel opens where it meets a slope or a cliff, and the hollow is lined: a
// ceiling over it, and a wall on each side where neither another hollow nor open air lies beyond. The lining is
// painted with the tile's cliff face.
//
// A piece flagged as a tunnel whose ground was lowered since no longer has ground over it; it hollows nothing.
#pragma once

#include "park/terrain_mesh.hpp"
#include "save/model/world.hpp"

#include <span>
#include <vector>

namespace rct3::park
{

    /// Room over a tunnel piece's highest point, in world units: below kTunnelCover, so a lip of ground stays
    /// over the opening.
    inline constexpr float kTunnelHeight = 2.75f;
    /// How far under a tunnel piece's lowest point its hollow starts, so the deck hides the floor's edge. Our
    /// own value.
    inline constexpr float kTunnelFloor = 0.1f;

    /// The hollows of the tunnel pieces among `paths`.
    [[nodiscard]] std::vector<GroundOpening> tunnel_hollows(std::span<const save::PathTile> paths, const save::TerrainGrid& grid);

    /// The lining of `hollows`, in cliff batches.
    [[nodiscard]] TerrainMesh tunnel_lining(std::span<const GroundOpening> hollows, const save::TerrainGrid& grid);

} // namespace rct3::park
