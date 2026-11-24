// SPDX-License-Identifier: GPL-3.0-or-later
//
// Actions wait in a queue for the tick they are stamped with, and apply at the start of it, before it is stepped.
// Whoever runs the park also applies what is due where a turn runs no tick (paused, or between ticks), so building
// while paused works; with nothing stepped in between, that is still the start of the same tick.
#pragma once

#include "simulation/actions/action.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace rct3::simulation::actions
{

    struct ActionQueue
    {
        struct Queued
        {
            u64 tick = 0; ///< The SimWorld::tick it applies at the start of
            Action action;
        };
        std::vector<Queued> waiting { };   ///< In the order asked
        std::vector<std::string> told { }; ///< Why actions applied were refused, as string keys, for the player
    };

    /// Queues `action` for the next tick `world` runs.
    void ask(ActionQueue& queue, const SimWorld& world, Action action);

    /// Applies every action due by `world`'s next tick, in the order asked, and drops them; later ones wait. Why one
    /// was refused goes on `told` where the player is to be told.
    void apply_due(ActionQueue& queue, World& world);

} // namespace rct3::simulation::actions
