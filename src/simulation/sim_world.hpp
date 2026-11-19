// SPDX-License-Identifier: GPL-3.0-or-later
//
// The simulated park: every entity and its components, and the clock and seed the run goes by. Two worlds
// with the same contents, seed and tick go on identically.
//
// Includes EnTT, which is large: include this only where the registry is needed.
#pragma once

#include "core/types.hpp"
#include "random/generator.hpp"
#include "simulation/entt.hpp"
#include "simulation/name_cheats.hpp"
#include "simulation/path_network.hpp"
#include "simulation/ratings.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace rct3::simulation
{

    /// What a step works out afresh each tick, kept between ticks so the lists' memory is reused instead of
    /// allocated again. Nothing in it outlasts the tick that fills it.
    struct StepBuffers
    {
        std::unordered_map<NodeId, std::vector<entt::entity>> litter_at { }; ///< Litter on each tile, in entity order
        std::unordered_map<NodeId, std::vector<entt::entity>> items_at { };  ///< Path items on each tile, in entity order
        std::vector<bool> guarded { };                                       ///< By tile: guards keep vandals off it
        std::vector<NodeId> ring { }, next { };
        std::vector<entt::entity> speakers { }; ///< The park's loudspeakers, in entity order
        /// A person walking the paths, in the grid cell they stand in, for keeping people apart.
        struct Body
        {
            i64 cell = 0;
            glm::vec3 at { 0.0f };
            entt::entity who = entt::null;
        };
        std::vector<Body> bodies { };     ///< By cell, then entity
        std::vector<glm::vec2> steps { }; ///< Each body's step aside this tick
    };

    struct SimWorld
    {
        entt::registry registry;
        PathNetwork paths;
        u64 tick = 0;                                 ///< Simulation steps run so far
        random::Seed seed;                            ///< Randomness of the whole run derives from it
        RatingModel ratings = builtin_rating_model(); ///< How rides untested in the park get rated
        StepBuffers buffers { };
        f32 guest_slowdown = 1;            ///< Developer switch: guests live this many times slower than the park
        bool parties_huddle_often = false; ///< Developer switch: a wandering party stands together at every rest
        NameCheats cheats { };             ///< Set off by renaming guests
    };

    /// How many times slower than the park guests live now: the developer's slow motion, quickened while guests
    /// are in a hurry.
    [[nodiscard]] inline f32 current_guest_slowdown(const SimWorld& world)
    {
        return world.guest_slowdown / (world.cheats.hurry.value > 0 ? kHurryPace : 1.0f);
    }

} // namespace rct3::simulation
