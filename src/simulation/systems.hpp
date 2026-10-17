// SPDX-License-Identifier: GPL-3.0-or-later
//
// One step of the park's life. Each step:
//
//  1. needs grow and moods drift, for every guest;
//  2. guests on the move walk, and a guest at a path tile with nothing to do picks something: a shop for
//     what they need most, now and then a flat ride, or else the next tile to wander to;
//  3. guests who reached the attraction they were heading for buy there or join its queue;
//  4. flat rides run their cycles: a stopped ride takes on the front of its queue and runs, and lets its
//     riders off when the cycle ends;
//  5. parties whose riders are all off go on together again.
//
// Followers in a party do not choose anything: after phase 2 they take their leader's step, each in a lane
// of their own. Phases 1 and 2 and the followers' step only write each guest's own components and are shared
// out over the workers; phases 3 to 5 change attractions and parties and run on one thread in ascending
// entity order. Every choice draws on the guest's own
// Dice, so a world steps the same way however many workers it has.
#pragma once

#include "simulation/coaster.hpp"
#include "simulation/sim_world.hpp"

#include <cstdint>
#include <span>

namespace rct3::simulation
{

    class Workers;

    inline constexpr float kTickSeconds = 0.25f; ///< Simulated time a step covers
    inline constexpr float kConsumeSeconds = 10; ///< A guest spends with what they bought before moving on; our own figure
    /// Farthest a guest walks off a path tile's middle, along x and z.
    /// shortcut: one width for every path, from how evenly saved guests spread to about this either side of
    /// the middle; upgrade when paths of other widths are modelled.
    inline constexpr float kLaneReach = 1.3f;

    struct Pose;
    /// Where `pose` is drawn `along` (0..1) of the way through the step after the last.
    [[nodiscard]] Pose between_steps(const Pose& pose, float along);

    /// Keeps a train this far behind the back of the one in front, metres; our own figure.
    inline constexpr float kBlockGap = 4.0f;

    /// The clear track ahead of train `t` of `trains`, up to kBlockGap short of the back of the nearest
    /// train in front; endless for a lone train.
    [[nodiscard]] float room_ahead(std::span<const Coaster> trains, std::size_t t);

    struct Departure;
    /// Whether a train (or a ride's next go) with `riders` of `capacity` seats aboard leaves after `waited`
    /// seconds of loading, as `departure` says.
    [[nodiscard]] bool departs(const Departure& departure, std::size_t riders, std::size_t capacity, float waited);

    /// Weight of the newest departure in a ride's riders an hour (CycleRide::per_hour); our own figure.
    inline constexpr float kFlowSmoothing = 0.25f;

    /// Seconds litter lies before it counts against the park's rating.
    inline constexpr float kOldLitterSeconds = 400;
    /// Pieces of litter lying long enough to count against the park's rating (kOldLitterSeconds).
    [[nodiscard]] std::int32_t old_litter(const SimWorld& world);

    /// Ticks in `seconds` of simulated time, rounded up.
    [[nodiscard]] std::uint32_t ticks(float seconds);

    /// Gives guest `guest` what it needs to walk, standing on path node `at` with nothing to do. Its dice are
    /// seeded from the world's seed and its id, so the same park always plays out the same way.
    void start_walking(SimWorld& world, entt::entity guest, NodeId at);

    /// Advances `world` by one tick, sharing work out over `workers` when given.
    void step(SimWorld& world, Workers* workers = nullptr);

} // namespace rct3::simulation
