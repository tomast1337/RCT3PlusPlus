// SPDX-License-Identifier: GPL-3.0-or-later
//
// The railings along queues. A park keeps only which fence a queue uses and its colours; the railings are
// built when it loads, as placed fence pieces on the edges of each queue tile, every side but the two the
// line of guests comes in and goes out by.
//
// On a queue laid on the ground each piece stands on the ground at the middle of its edge and leans with
// the edge's two corners. On a level raised queue it stands at the path's height. On a sloping queue piece
// only the two sides along the slope are fenced: each stands at the height halfway up and leans with the
// slope. A sloping piece's stored height is its start end's, and its slope says whether it climbs or falls
// from there towards its end.
#pragma once

#include "park/ground_queues.hpp"
#include "park/object_mesh.hpp"
#include "save/model/world.hpp"

#include <span>
#include <vector>

namespace rct3::park
{

    /// The quarter turn that points an edge piece's outward side at `side` of its tile.
    [[nodiscard]] std::int32_t edge_turns(TileSide side) noexcept;

    /// True for the slopes of path pieces that fall from their start towards their end.
    [[nodiscard]] bool slope_falls(std::uint8_t slope) noexcept;

    struct QueueFences
    {
        std::vector<save::PlacedObject> pieces;
        Leans leans; ///< Of the pieces that do not stand level
    };

    /// Placed fence pieces along the queues among `paths`. New objects take ids after `last_id`.
    [[nodiscard]] QueueFences queue_fences(std::span<const save::PathTile> paths, const save::TerrainGrid& grid, std::uint64_t last_id);

} // namespace rct3::park
