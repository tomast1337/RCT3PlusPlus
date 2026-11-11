// SPDX-License-Identifier: GPL-3.0-or-later
//
// How staff look at work, by the names the game's staff meshes give their motions. Most tools are pieces of a
// worker's body that every motion places, tucked away or in the hand: a mechanic's spanner, a guard's truncheon,
// a keeper's shovel, a handyman's bin bag. A handyman's broom and watering can are shapes of their own, held in
// the hand socket only while the job lasts.
#pragma once

#include "simulation/animals.hpp"
#include "simulation/components.hpp"

#include <string_view>

namespace rct3::simulation::staff
{

    /// A job's motions: one played once as it starts, one repeated through it, one played once as it ends;
    /// empty where the job has none. And the tool shape held in the hand, empty for none.
    struct WorkLook
    {
        std::string_view start { };
        std::string_view loop { };
        std::string_view finish { };
        std::string_view tool { };

        bool operator==(const WorkLook&) const = default;
    };

    /// How a worker at `job` looks; a mechanic fixing a ride by how it broke down (`breakdown`). Nothing to
    /// play for StaffJob::None.
    [[nodiscard]] WorkLook work_look(StaffJob job, Breakdown breakdown);

    /// The motion of a keeper at `job`, walking or standing: the troughs' container to fill them, else the shovel.
    [[nodiscard]] std::string_view keeper_motion(KeeperJob job, bool walking);

} // namespace rct3::simulation::staff
