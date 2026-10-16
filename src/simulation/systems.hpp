// SPDX-License-Identifier: GPL-3.0-or-later
//
// One step of the park's life. Each step:
//
//  1. needs grow and moods drift, for every guest;
//  2. guests on the move walk, and a guest at a path tile with nothing to do picks something: a shop for
//     what they need most, now and then a flat ride, or else the next tile to wander to;
//  3. guests who reached the attraction they were heading for buy there or join its queue;
//  4. flat rides run their cycles: a stopped ride takes on the front of its queue and runs, and lets its
//     riders off when the cycle ends.
//
// Phases 1 and 2 only write each guest's own components and are shared out over the workers; phases 3 and 4
// change attractions and run on one thread in ascending entity order. Every choice draws on the guest's own
// Dice, so a world steps the same way however many workers it has.
#pragma once

#include "simulation/sim_world.hpp"

#include <cstdint>

namespace rct3::simulation
{

    class Workers;

    inline constexpr float kTickSeconds = 0.25f; ///< Simulated time a step covers

    /// Ticks in `seconds` of simulated time, rounded up.
    [[nodiscard]] std::uint32_t ticks(float seconds);

    /// Gives guest `guest` what it needs to walk, standing on path node `at` with nothing to do. Its dice are
    /// seeded from the world's seed and its id, so the same park always plays out the same way.
    void start_walking(SimWorld& world, entt::entity guest, NodeId at);

    /// Advances `world` by one tick, sharing work out over `workers` when given.
    void step(SimWorld& world, Workers* workers = nullptr);

} // namespace rct3::simulation
