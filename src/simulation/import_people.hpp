// SPDX-License-Identifier: GPL-3.0-or-later
//
// Brings the people of an original save into the simulation, as entities with our own components. Only
// what the simulation models is carried over; the rest of the original's bookkeeping is left behind.
#pragma once

#include "save/model/people.hpp"
#include "simulation/sim_world.hpp"

namespace rct3::simulation
{

    /// Adds an entity for each guest of `people` that is in the park.
    void import_guests(SimWorld& world, const save::PeopleModel& people);

} // namespace rct3::simulation
