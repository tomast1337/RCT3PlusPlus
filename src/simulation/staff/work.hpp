// SPDX-License-Identifier: GPL-3.0-or-later
//
// Staff at work, each tick: paid as each week ends; at their posts, content or not; and on the paths, where
// mechanics are sent to broken rides and those due an inspection, handymen sweep, mend, empty bins and water
// beds, guards keep watch, inspectors go on their rounds, and all of them keep to their patrols.
#pragma once

#include "simulation/components.hpp"
#include "simulation/entt.hpp"
#include "units/units.hpp"

#include <cstdint>
#include <tuple>
#include <utility>
#include <vector>

namespace rct3::simulation
{
    struct SimWorld;
}

namespace rct3::simulation::staff
{

    /// Where an inspector is in their rounds. On a visit they carry a guest's Needs, Mood and Wallet, and the
    /// guests' code walks, queues and rides them; let go, they are staff again.
    struct Rounds
    {
        std::uint8_t task = 0;  ///< The next of their tasks to look at, by its bit among the task flags
        units::Seconds gap { }; ///< Before they next look for somewhere to go
        /// The attractions visited and workers watched, with when (seconds into the run).
        std::vector<std::pair<entt::entity, float>> seen { };
        entt::entity watching = entt::null; ///< The worker they are after, or watching
        float chased_from = 0;              ///< When they set off after them
        float watched_from = -1;            ///< When they came close enough to watch; -1 not yet
        /// What they told the player about what, with when: not told again for a while.
        std::vector<std::tuple<entt::entity, Finding, float>> told { };
    };

    /// One tick of the staff's work, after the guests have walked and dropped their litter. Run as part of the
    /// "Tidy" phase.
    void step_work(SimWorld& world);

} // namespace rct3::simulation::staff
