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
// Tracked rides only get the walls of their entrances and exits here. A few of their pieces (tower and dark
// ride bases) are fenced round like flat rides in the game, which this does not do yet.
#pragma once

#include "park/object_models.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"

#include <cstdint>
#include <vector>

namespace rct3::park
{

    /// Placed fence pieces round `ride`. `item` is the model of the object a flat ride is built from, if it was
    /// found; without it the ride is not fenced round. New objects take ids after `last_id`.
    [[nodiscard]] std::vector<save::PlacedObject> ride_fences(const save::Ride& ride, const ObjectModel* item, std::uint64_t last_id);

} // namespace rct3::park
