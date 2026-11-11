// SPDX-License-Identifier: GPL-3.0-or-later
//
// Holding people: the player picks a guest or worker up and lets them go, a crash throws them, steep ground
// slides them; then they fall, land and walk back to the paths. The step that lands them, and what takes
// people out of what they were doing.
#pragma once

#include "math/glm.hpp"
#include "profiling/profiler.hpp"
#include "simulation/entt.hpp"
#include "simulation/path_network.hpp"

#include <optional>

namespace rct3::simulation
{

    struct SimWorld;

    /// The player picks `person` up, guest or staff, out of what they were doing: a guest leaves their queue
    /// and their party, a mechanic the ride they were going to, a swimmer the pool, keeping their swimwear on
    /// till they land. False, and nothing done, for one who cannot be picked up now: on a ride, in a booth,
    /// changing, gone home or held already.
    bool pick_up(SimWorld& world, entt::entity person);

    /// The player lets a held person go over `ground`, a point on the ground, a path or a pool complex: they
    /// fall to it and walk to the nearest path to go on from, or, a guest let go over a pool complex, swim there
    /// as if they had come in through its changing room. False, and still held, where there are no paths to
    /// walk to.
    bool let_go(SimWorld& world, entt::entity person, glm::vec3 ground);

    /// Where a worker just hired goes when the player gives them back without putting them down: the middle of
    /// the walkway staff may use nearest the park's entrance. None without an entrance or such a walkway.
    [[nodiscard]] std::optional<glm::vec3> entrance_drop(const SimWorld& world);

    /// The ground under `person`, on foot, grew too steep: they slide down it, keeping to the ground, till it
    /// levels out enough to stop them, fall over, get up and walk to the nearest path. False, and nothing done,
    /// for one not on foot: riding, swimming, seated, in a booth, held or already falling.
    bool slide(SimWorld& world, entt::entity person);

    /// Throws `person`, on foot or just let out of a crashed ride, from `at` at `velocity`: they fly, land
    /// knocked over, lie there a while, then walk to `node` and go on from there.
    void throw_person(entt::registry& r, entt::entity person, glm::vec3 at, glm::vec3 velocity, NodeId node);

    /// Takes `person` out of what they were doing on foot: out of the queue they stood in, away from their party
    /// till they land (rejoin), and, a mechanic, off the job in hand.
    void step_out(entt::registry& r, entt::entity person);

    /// The path under `ground`, else the nearest one a person there may walk on; nothing without paths.
    /// shortcut: off the paths they walk straight to it, over whatever lies between; route round scenery once
    /// the ground is walkable.
    [[nodiscard]] std::optional<NodeId> path_to_rejoin(const PathNetwork& paths, glm::vec3 ground);

    /// One tick of those let go of or thrown: they fall to the ground, or slide down it, then walk straight to
    /// the path they go on from; landed in a pool complex, they swim there. Times the phase on `phase`.
    void step_let_go(SimWorld& world, std::optional<profiling::Scope>& phase);

} // namespace rct3::simulation
