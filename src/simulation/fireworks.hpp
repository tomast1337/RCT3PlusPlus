// SPDX-License-Identifier: GPL-3.0-or-later
//
// Guests watching a firework display, or a fountain show (a display of water jets). While one goes up, guests walking the paths near it stop where they are,
// turn to it and watch, cheered a little all the while as at any sight. When it ends they cheer, think well of
// what they saw and are lifted by it, twice as much when it went up in the dark, then walk on.
//
// A stage (a show ride) is watched the same way while its performers do their show: guests passing close by
// stop, and walk on when the show ends, lifted less than its seated audience, by day or night alike.
//
// Reach and lift are our own design, chosen by eye: everyone with a clear view of the sky over the launchers
// stops, and a show lifts about as much as a good ride.
#pragma once

#include "math/glm.hpp"
#include "simulation/components.hpp"
#include "simulation/sim_world.hpp"
#include "units/units.hpp"

namespace rct3::simulation
{

    /// A display going up, in the registry's context while it plays.
    struct FireworkShow
    {
        glm::vec3 centre { 0.0f }; ///< Among its launchers
        bool dark = false;         ///< Going up at night
        /// What its watchers think as it ends: by what most of its shots are.
        ThoughtKind thought = ThoughtKind::GreatFireworks;
    };

    /// Guests no farther than this from a show, across the ground, stop to watch it.
    inline constexpr units::Distance kFireworksReach { 120 };
    /// Happiness a show gives each watcher as it ends, by day; at night twice this.
    inline constexpr units::Share kFireworksLift { 0.1f };

    /// A show ride, on the ride's entity: while it runs, its performers put on a show passers-by stop for.
    struct Stage
    {
        glm::vec3 centre { 0.0f }; ///< Where its performers are
    };

    /// Guests no farther than this from a stage, across the ground, stop to watch its show.
    inline constexpr units::Distance kStageReach { 30 };
    /// Happiness a stage's show gives each passer-by who watched it, as it ends: watched for free from outside,
    /// half a display's.
    inline constexpr units::Share kStageLift { 0.05f };

    /// Marks a guest stopped to watch the show, so its end lets them go.
    struct FireworkWatcher
    {
        bool dark = false; ///< Saw it in the dark
        ThoughtKind thought = ThoughtKind::GreatFireworks;
        entt::entity stage = entt::null; ///< The show ride watched; none for a display
    };

    /// Stops guests near the show in the context, or near a stage while its show runs, to watch it, facing it, and
    /// lets them go, cheered, once the show they watched is over. Guests already stopped, busy at an attraction or
    /// held are left alone.
    void watch_fireworks(SimWorld& world);

} // namespace rct3::simulation
