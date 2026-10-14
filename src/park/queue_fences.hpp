// SPDX-License-Identifier: GPL-3.0-or-later
//
// The railings along queues. A park keeps only which fence a queue uses and its colours; the railings are
// built when it loads, as placed fence pieces on the edges of each queue tile, every side but the two the
// line of guests comes in and goes out by. On a queue laid on the ground each piece stands on the ground
// at its edge; on a level raised queue it stands at the path's height. Sloping queue pieces get none yet.
#pragma once

#include "park/ground_queues.hpp"
#include "save/model/world.hpp"

#include <span>
#include <vector>

namespace rct3::park
{

    /// The quarter turn that points an edge piece's outward side at `side` of its tile.
    [[nodiscard]] std::int32_t edge_turns(TileSide side) noexcept;

    /// Placed fence pieces along the queues among `paths`. New objects take ids after `last_id`.
    [[nodiscard]] std::vector<save::PlacedObject> queue_fences(std::span<const save::PathTile> paths, std::uint64_t last_id);

} // namespace rct3::park
