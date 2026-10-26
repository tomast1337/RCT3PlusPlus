// SPDX-License-Identifier: GPL-3.0-or-later
//
// The fence round the park. A park names one fence; its panels are built when it loads, on the edges of the
// land the park owns outright (not land it holds only the building rights to): every edge whose tile across
// is not owned that way, or lies off the map. Where a path runs across such an edge, with path on the tiles
// either side, the edge is left open for guests to walk through. Each panel stands on the ground at the
// middle of its edge and leans with the edge, as the railings of queues on the ground do.
#pragma once

#include "park/object_mesh.hpp"
#include "save/model/world.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rct3::park
{

    struct BoundaryFence
    {
        std::vector<save::PlacedObject> pieces;
        Leans leans; ///< Of the pieces that do not stand level
    };

    /// Whether the park owns `cell` outright.
    [[nodiscard]] bool owned_outright(const save::TerrainCell& cell) noexcept;

    /// How a fence panel on a tile's edge leans with the ground under that edge as it is now: by the rise
    /// between the edge's two corners, so it follows the ground when the ground is reshaped.
    [[nodiscard]] glm::vec2 panel_lean(const save::TerrainGrid& grid, const save::PlacedObject& panel);

    /// The fence `symbol` from `overlay` round the land the park owns in `grid`, open where `paths` cross its
    /// edge. None when `symbol` is empty. New objects take ids after `last_id`.
    [[nodiscard]] BoundaryFence boundary_fence(const std::string& symbol, const std::string& overlay, const save::TerrainGrid& grid, std::span<const save::PathTile> paths, std::uint64_t last_id);

} // namespace rct3::park
