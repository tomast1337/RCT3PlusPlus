// SPDX-License-Identifier: GPL-3.0-or-later
//
// The fences round rides. A park keeps only which fence a ride uses and its three colours; the panels are
// built when it loads, as placed fence pieces on tile edges, standing level at the height of what they fence.
//
//  - A ride's stand-in fence, when it has one (a temporary fence while it is out of use), takes the place of
//    its own. A ride with neither has no fences at all.
//  - Every station's entrance and exit is walled on its two sides: the edges either side of the way it faces.
//    Its front stays open to the ride and its back to the queue or path.
//  - A flat ride is also fenced round its footprint: every square on the rim gets a panel on its outer edge,
//    except where the tile across that edge holds the first station's entrance or exit. Rides that stand
//    only on water are not fenced round, and some rides are fenced round the outline of their solid ground
//    squares instead of their whole rectangle.
//
// A tracked ride's pieces that stand on ground of their own (a tower's or a house's base) are fenced round the
// same way, their gaps at the first station's doors; its other pieces are not fenced.
#pragma once

#include "park/object_models.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace rct3::park
{

    /// The model of the placed object of that id, if it was found.
    using ModelOf = std::function<const ObjectModel*(std::uint64_t object)>;

    /// Placed fence pieces round `ride`. `model_of` gives the models of the object a flat ride is built from and
    /// of its track pieces' objects; one not found is not fenced round. New objects take ids after `last_id`.
    [[nodiscard]] std::vector<save::PlacedObject> ride_fences(const save::Ride& ride, const ModelOf& model_of, std::uint64_t last_id);

} // namespace rct3::park
