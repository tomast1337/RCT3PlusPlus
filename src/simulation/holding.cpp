// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/holding.hpp"

#include "simulation/components.hpp"
#include "simulation/crash.hpp"
#include "simulation/guests.hpp"
#include "simulation/parties.hpp"
#include "simulation/ride_walks.hpp"
#include "simulation/rides.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/swimming.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace rct3::simulation
{

    void throw_person(entt::registry& r, entt::entity person, glm::vec3 at, glm::vec3 velocity, NodeId node)
    {
        if (!r.valid(person) || r.any_of<Left, Held, Dropped>(person) || !r.all_of<Errand, Stride, Pose>(person) || node == kNoNode)
            return;
        step_out(r, person);
        r.remove<Errand, Stride, Linger, Gesture, Watching, RideWalk>(person);
        auto& pose = r.get<Pose>(person);
        pose.position = pose.was = at;
        const auto* surroundings = r.ctx().find<WreckSurroundings>();
        const float ground = surroundings != nullptr && surroundings->ground ? surroundings->ground({ at.x, at.z }) : 0.0f;
        r.emplace<Dropped>(person, Dropped { .ground = ground, .fall = { -velocity.y }, .drift = { velocity.x, velocity.z }, .thrown = true, .node = node });
    }

    void step_out(entt::registry& r, entt::entity person)
    {
        const Errand& errand = r.get<Errand>(person);
        if (errand.kind == ErrandKind::Queueing && r.valid(errand.target))
            if (auto* ride = r.try_get<CycleRide>(errand.target))
                std::erase(ride->queue, person);
        // The next member leads meanwhile; the others wait for them (5.).
        if (const auto* member = r.try_get<Member>(person); member != nullptr && r.valid(member->party))
        {
            Party& party = r.get<Party>(member->party);
            std::erase(party.members, person);
            party.away.push_back(person);
            if (party.leader == person)
            {
                party.leader = party.members.empty() ? entt::null : party.members.front();
                if (party.leader != entt::null)
                    if (Errand& next = r.get<Errand>(party.leader); next.kind == ErrandKind::Following)
                        next = { ErrandKind::Wandering, entt::null, 0 };
            }
        }
        r.remove<Lost>(person);
        r.remove<Member>(person);
        // A mechanic drops the job in hand, and another is sent.
        if (auto* staff = r.try_get<Staff>(person))
        {
            if (auto* upkeep = r.valid(staff->ride) ? r.try_get<Upkeep>(staff->ride) : nullptr; upkeep != nullptr && upkeep->mechanic == person)
                upkeep->mechanic = entt::null;
            staff->ride = entt::null;
            staff->busy = { };
            staff->job = StaffJob::None;
        }
    }

    std::optional<NodeId> path_to_rejoin(const PathNetwork& paths, glm::vec3 ground)
    {
        std::optional<NodeId> node = paths.nearest(ground);
        const auto& nodes = paths.nodes();
        if (node && !nodes[*node].queue && !nodes[*node].removed && std::abs(nodes[*node].height - ground.y) <= 1)
            return node;
        node.reset();
        float best = std::numeric_limits<float>::max();
        for (NodeId n = 0; n < nodes.size(); ++n)
            if (const float d = glm::distance(paths.centre(n), ground); !nodes[n].queue && !nodes[n].removed && d < best)
            {
                best = d;
                node = n;
            }
        return node;
    }

    bool pick_up(SimWorld& world, entt::entity person)
    {
        entt::registry& r = world.registry;
        if (!r.valid(person) || r.any_of<Left, Held, Dropped, RideWalk>(person) || !r.all_of<Errand, Stride, Pose, Dice>(person))
            return false;
        Errand& errand = r.get<Errand>(person);
        const auto* swimmer = r.try_get<Swimmer>(person);
        if (errand.kind == ErrandKind::Riding || (errand.kind == ErrandKind::Swimming && (swimmer == nullptr || swimmer->changing.value > 0)))
            return false;
        step_out(r, person);
        auto& dice = r.get<Dice>(person).state;
        const auto& holding = kTuning.holding;
        const Held held { .height = { static_cast<float>(random::between(dice, holding.lowest.value, holding.highest.value)) }, .happy = random::chance(dice, holding.happy.value), .swimwear = in_swimwear(r, person) };
        if (r.all_of<Visitor>(person))
        {
            constexpr std::array kSay { ThoughtKind::PickedUpHappy, ThoughtKind::PickedUpNeutral, ThoughtKind::PickedUpSad };
            think(r, person, kSay[random::below(dice, kSay.size())], entt::null, static_cast<float>(world.tick) * kTickSeconds);
        }
        r.remove<Errand, Stride, Linger, Gesture, Watching, Swimmer, PoolRoute>(person);
        r.emplace<Held>(person, held);
        return true;
    }

    bool let_go(SimWorld& world, entt::entity person, glm::vec3 ground)
    {
        entt::registry& r = world.registry;
        if (!r.valid(person) || !r.all_of<Held, Pose>(person))
            return false;
        const std::optional<NodeId> node = path_to_rejoin(world.paths, ground);
        if (!node)
            return false;
        // Only guests swim; staff let go over a pool walk off it to the paths.
        const auto door = r.all_of<Visitor>(person) ? pool_door_at(world, ground) : std::nullopt;
        const bool swimwear = r.get<Held>(person).swimwear;
        r.remove<Held>(person);
        r.emplace<Dropped>(person, Dropped { .ground = ground.y, .node = *node, .swimwear = swimwear, .pool = door.value_or(entt::null) });
        return true;
    }

    std::optional<glm::vec3> entrance_drop(const SimWorld& world)
    {
        const auto gates = world.registry.view<const ParkGate>();
        if (gates.empty())
            return std::nullopt;
        const glm::vec3 gate = world.paths.centre(gates.get<const ParkGate>(gates.front()).node);
        std::optional<glm::vec3> best;
        for (NodeId n = 0; n < world.paths.nodes().size(); ++n)
        {
            const PathNode& node = world.paths.nodes()[n];
            if (node.removed || node.queue || node.staff_kept_off)
                continue;
            const glm::vec3 at = world.paths.centre(n);
            if (!best || glm::distance(at, gate) < glm::distance(*best, gate))
                best = at;
        }
        return best;
    }

    bool slide(SimWorld& world, entt::entity person)
    {
        entt::registry& r = world.registry;
        if (!r.valid(person) || r.any_of<Linger, Swimmer>(person) || !r.all_of<Errand>(person))
            return false;
        if (const ErrandKind kind = r.get<Errand>(person).kind; kind == ErrandKind::Riding || kind == ErrandKind::Swimming)
            return false;
        if (const auto* staff = r.try_get<Staff>(person); staff != nullptr && staff->role == StaffRole::Vendor)
            return false;
        const Pose& pose = r.get<Pose>(person);
        throw_person(r, person, pose.position, { }, r.get<Stride>(person).to);
        auto* drop = r.try_get<Dropped>(person);
        if (drop == nullptr)
            return false;
        drop->thrown = false;
        drop->sliding = true;
        return true;
    }

    void step_let_go(SimWorld& world, std::optional<profiling::Scope>& phase)
    {
        entt::registry& r = world.registry;
        const PathNetwork& paths = world.paths;
        constexpr float dt = kTickSeconds;

        phase.emplace("Let go");
        // Those the player let go of fall to the ground, then walk straight to the path they go on from. Those
        // a crash threw fly on as they fall, unslowed, and lie where they land till they get up.
        const auto* surroundings = r.ctx().find<WreckSurroundings>();
        for (const entt::entity e : sorted(r.view<Dropped>()))
        {
            auto& drop = r.get<Dropped>(e);
            auto& pose = r.get<Pose>(e);
            // Sliding, the slope pulls them down it and the ground's rub slows them, till they stop on gentler
            // ground, fall over and look for the nearest path.
            if (drop.sliding)
            {
                const auto& slides = kTuning.slides;
                const float gravity = kTuning.holding.gravity;
                const glm::vec2 rise = surroundings != nullptr ? slope(surroundings->ground, { pose.position.x, pose.position.z }) : glm::vec2 { };
                const float lean = 1 + glm::dot(rise, rise); // The steeper, the less of their weight presses on the ground
                drop.drift -= rise * (gravity / lean * dt);
                const float speed = glm::length(drop.drift);
                const float rub = slides.grip * gravity / lean * dt;
                drop.drift = speed > rub ? drop.drift * (1 - rub / speed) : glm::vec2 { };
                pose.position += glm::vec3 { drop.drift.x, 0, drop.drift.y } * dt;
                drop.ground = surroundings != nullptr && surroundings->ground ? surroundings->ground({ pose.position.x, pose.position.z }) : pose.position.y;
                pose.position.y = drop.ground;
                if ((glm::length(drop.drift) < slides.settled.value && glm::length(rise) <= slides.steep) || (drop.slid.value += dt) >= slides.longest.value)
                {
                    drop.sliding = false;
                    drop.drift = { };
                    drop.node = path_to_rejoin(paths, pose.position).value_or(drop.node);
                    // Staff have no motion for lying down, so they only stand up again.
                    if (r.all_of<Visitor>(e))
                        r.emplace_or_replace<Gesture>(e, GestureKind::Down, kTuning.crashes.knocked);
                }
                continue;
            }
            if (drop.thrown || pose.position.y > drop.ground)
            {
                drop.fall.value += kTuning.holding.gravity * dt;
                if (!drop.thrown)
                    drop.fall.value = std::min(drop.fall.value, kTuning.holding.fastest_fall.value);
                glm::vec2 across = glm::vec2 { pose.position.x, pose.position.z } + drop.drift * dt;
                // Off a slide's end into a pool, its far wall stops them, and they go on only down.
                const auto* door = r.valid(drop.pool) ? r.try_get<PoolDoor>(drop.pool) : nullptr;
                if (const auto* pool = door != nullptr && r.valid(door->pool) ? r.try_get<SwimPool>(door->pool) : nullptr; pool != nullptr && r.valid(drop.slide))
                    if (const glm::vec2 stop = pool_stop(*pool, { pose.position.x, pose.position.z }, across, paths.tile_size); stop != across)
                    {
                        across = stop;
                        drop.drift = { };
                    }
                pose.position.x = across.x;
                pose.position.z = across.y;
                if (drop.thrown && surroundings != nullptr && surroundings->ground)
                    drop.ground = surroundings->ground({ pose.position.x, pose.position.z });
                pose.position.y = std::max(pose.position.y - drop.fall.value * dt, drop.ground);
                if (pose.position.y <= drop.ground && drop.fall.value >= 0 && drop.thrown)
                {
                    drop.thrown = false;
                    drop.drift = { };
                    r.emplace_or_replace<Gesture>(e, GestureKind::Down, kTuning.crashes.knocked);
                }
                continue;
            }
            if (r.all_of<Gesture>(e))
                continue;
            // Landed in a pool complex: they swim there, in their swimwear, as if they had paid and changed at
            // its room, and leave by it. Away from their party, they go on alone after.
            if (const auto* door = r.valid(drop.pool) ? r.try_get<PoolDoor>(drop.pool) : nullptr; door != nullptr && r.all_of<Attraction>(drop.pool))
            {
                const entt::entity room = drop.pool;
                const NodeId door_node = r.get<Attraction>(room).entrance != kNoNode ? r.get<Attraction>(room).entrance : drop.node;
                const glm::vec3 landed = pose.position;
                const Dice dice = r.get<Dice>(e);
                const auto* swam = r.try_get<Swimmer>(e); // Off one of its slides, with the time they had left
                const Seconds left = swam != nullptr ? swam->left : kTuning.pools.stay;
                r.remove<Dropped>(e);
                start_walking(world, e, door_node);
                r.get<Dice>(e) = dice;
                pose.position = pose.was = landed;
                r.get<Errand>(e) = { .kind = ErrandKind::Swimming, .target = room };
                // In the water, or on the paving round it, as they came down.
                const auto* pool = r.valid(door->pool) ? r.try_get<SwimPool>(door->pool) : nullptr;
                const bool wet = pool != nullptr && std::ranges::any_of(pool->water, [&](const glm::vec3& w) { return std::abs(w.x - landed.x) <= paths.tile_size / 2 && std::abs(w.z - landed.z) <= paths.tile_size / 2; });
                r.emplace_or_replace<Swimmer>(e, Swimmer { .changed = true, .left = left, .goal = landed, .in_water = wet });
                continue;
            }
            const glm::vec3 to = paths.centre(drop.node) - pose.position;
            const float left = glm::length(glm::vec2(to.x, to.z));
            if (left > kLaneReach)
            {
                const auto* visitor = r.try_get<Visitor>(e);
                const float pace = (visitor != nullptr ? kTuning.walking.speed[std::to_underlying(visitor->age)] : Stride { }.speed).value * dt;
                pose.heading.value = std::atan2(to.x, to.z);
                pose.position += glm::vec3 { to.x, 0, to.z } * (std::min(pace, left) / left); // Over the ground they landed on
                continue;
            }
            const NodeId node = drop.node;
            const Dice dice = r.get<Dice>(e); // Going on as they were, not from the start again
            r.remove<Dropped>(e);
            start_walking(world, e, node);
            r.get<Dice>(e) = dice;
            // Back with their party, if it is still in the park: they walk over to where it waits (5.), or lead it.
            for (const auto [p, party] : r.view<Party>().each())
            {
                if (std::erase(party.away, e) == 0)
                    continue;
                party.members.push_back(e);
                r.emplace<Member>(e, p);
                if (party.leader == entt::null)
                    party.leader = e;
                else
                    r.get<Errand>(e) = { .kind = ErrandKind::Waiting };
                break;
            }
        }
    }

} // namespace rct3::simulation
