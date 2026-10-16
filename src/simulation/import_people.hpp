// SPDX-License-Identifier: GPL-3.0-or-later
//
// Brings the people of an original save into the simulation, as entities with our own components. Only
// what the simulation models is carried over; the rest of the original's bookkeeping is left behind.
#pragma once

#include "save/model/people.hpp"
#include "simulation/sim_world.hpp"

#include <span>
#include <vector>

namespace rct3::simulation
{

    /// Adds an entity for each guest of `people` that is in the park; returns them by the guests' places in
    /// `people.guests`, null for those left out.
    std::vector<entt::entity> import_guests(SimWorld& world, const save::PeopleModel& people);

    /// Makes a Party of each group of `people` with two or more guests walking (see start_guests), `guests`
    /// being import_guests's answer. Those not in a queue or on a ride follow the group's leader. Returns
    /// how many parties were made.
    std::size_t import_parties(SimWorld& world, const save::PeopleModel& people, std::span<const entt::entity> guests);

} // namespace rct3::simulation
