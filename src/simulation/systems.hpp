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
#include "simulation/components.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

namespace rct3::simulation
{

    class Workers;

    inline constexpr float kTickSeconds = 0.25f; ///< Simulated time a step covers
    /// Farthest a guest walks off a path tile's middle, along x and z.
    /// shortcut: one width for every path, from how evenly saved guests spread to about this either side of
    /// the middle; upgrade when paths of other widths are modelled.
    inline constexpr float kLaneReach = 1.3f;
    inline constexpr units::Seconds kLongQueue { 240 }; ///< Of queueing, that feel like ages

    struct Pose;
    /// Where `pose` is drawn `along` (0..1) of the way through the step after the last.
    [[nodiscard]] Pose between_steps(const Pose& pose, float along);

    /// How far a water cannon pointing along `heading` at rest turns, round the vertical, to point along `way`
    /// (x, z): radians, the way Pose's heading grows; none when that is more than `most` to either side, as
    /// behind it.
    [[nodiscard]] std::optional<float> cannon_turn(float heading, glm::vec2 way, float most);

    /// Pieces of litter lying long enough to count against the park's rating (tuning::Litter::old).
    [[nodiscard]] std::int32_t old_litter(const SimWorld& world);

    /// The broken path items, and what putting each back new comes to at `price` (by its object id, the placed
    /// object it stands as). Our own design, as replace_broken.
    struct Replacement
    {
        std::uint32_t items = 0;
        units::Money cost { };
    };
    [[nodiscard]] Replacement replacement(const SimWorld& world, const std::function<units::Money(std::uint64_t object)>& price);

    /// Puts every broken path item back new at once. The player pays the whole price of each (replacement), the
    /// quick and dear way; a handyman mends one for nothing, given time.
    void replace_broken(SimWorld& world);

    /// Takes the ride or shop `attraction` out of the park: its queue walks off from its entrance, its riders
    /// from its exit, and those on their way to it or waiting for it go on to something else.
    void pull_down(SimWorld& world, entt::entity attraction);

    /// Whether a time of day (a fraction from midnight) is dark: from 20:00 to 6:00, our own hours.
    [[nodiscard]] bool is_night(float time);

    /// Ticks in `seconds` of simulated time, rounded up.
    [[nodiscard]] std::uint32_t ticks(float seconds);

    /// The inspector tells the player `finding` about `about` at `now`, unless they told it lately.
    void tell(entt::registry& r, entt::entity inspector, Finding finding, entt::entity about, float now);

    /// The guest thinks `kind` about `about` at `now` (seconds into the run), unless they thought it lately.
    void think(entt::registry& r, entt::entity guest, ThoughtKind kind, entt::entity about, float now);

    /// Stands `pose` where `s` has got to between its two tiles, in its lanes, on the path's surface.
    void place(const PathNetwork& paths, const Stride& s, Pose& pose);

    /// Stops `stride` at `node`, its route dropped, and stands `pose` there.
    void stop_at(const PathNetwork& paths, NodeId node, Stride& stride, Pose& pose);

    /// The entities of `view`, in ascending order, so serial phases go the same way every time.
    template<typename View> std::vector<entt::entity> sorted(const View& view)
    {
        auto out = view | std::ranges::to<std::vector<entt::entity>>();
        std::ranges::sort(out);
        return out;
    }

    /// Advances `world` by one tick, sharing work out over `workers` when given.
    void step(SimWorld& world, std::optional<Workers&> workers = std::nullopt);

} // namespace rct3::simulation
