// SPDX-License-Identifier: GPL-3.0-or-later
//
// The simulation side of the developer windows' Debug tab: one-step changes that put the simulated park in a
// state at once, so a case can be set up without waiting for the park to get there. Each keeps the park whole:
// what points at what it changes (queues, riders, parties, keepers' tasks) is put right with it.
#pragma once

#include "simulation/research.hpp"

#include <cstddef>
#include <vector>

namespace rct3::simulation
{

    struct SimWorld;

    /// Invents every item of `items` at once, at `time` of the calendar, in their order; `items` is left empty and
    /// nothing is worked on or focused.
    void invent_everything(Research& research, std::vector<ResearchItem>& items, float time);

} // namespace rct3::simulation
