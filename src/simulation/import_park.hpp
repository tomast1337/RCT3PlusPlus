// SPDX-License-Identifier: GPL-3.0-or-later
//
// Brings the footpaths and stalls of an original save into the simulation, and sets the imported guests
// walking on those paths.
#pragma once

#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "simulation/sim_world.hpp"

#include <cstddef>

namespace rct3::simulation
{

    /// Adds a node for every ground, raised and queue path tile of `world_model`, laid on its terrain.
    void import_paths(SimWorld& world, const save::WorldModel& world_model);

    /// Adds a shop for every stall of `rides` that sells a drink or food and stands beside a path; returns how
    /// many were added. Call after import_paths.
    std::size_t import_stalls(SimWorld& world, const save::RideModel& rides);

    /// Sets every guest standing on a path walking; returns how many. Call after import_paths.
    std::size_t start_guests(SimWorld& world);

} // namespace rct3::simulation
