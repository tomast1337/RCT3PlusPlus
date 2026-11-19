// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/wild_birds.hpp"

#include "simulation/guests.hpp"
#include "simulation/systems.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace rct3::simulation
{

    namespace
    {
        constexpr const BirdRates& kRates = kBirdRates;

        /// Gives a gull at an empty park no litter to come down at, and the park a spacing between gulls: the more
        /// litter, the sooner the next gull. Chosen by eye.
        constexpr float kLeastGullWait = 0.1f;
        constexpr float kGullWait = 4.0f;
        /// This much litter makes a gull sure to come down at some of it. Chosen by eye.
        constexpr float kLitterSure = 6.0f;

        glm::vec2 flat(glm::vec3 p)
        {
            return { p.x, p.z };
        }

        float ground_at(const BirdGround& ground, glm::vec2 p)
        {
            return ground.height ? ground.height(p) : 0.0f;
        }

        bool over_water(const BirdGround& ground, glm::vec2 p)
        {
            return ground.water && ground.water(p).has_value();
        }

        /// Where a bird stands at `p`: the water's surface over a lake, else the ground.
        float surface_at(const BirdGround& ground, glm::vec2 p)
        {
            const auto water = ground.water ? ground.water(p) : std::nullopt;
            return water ? *water : ground_at(ground, p);
        }

        /// The height a bird flies at over `p`.
        float flying_height(const BirdGround& ground, glm::vec2 p)
        {
            return surface_at(ground, p) + kRates.cruise_above.value;
        }

        glm::vec2 forward_of(float heading)
        {
            return { std::sin(heading), std::cos(heading) };
        }

        glm::vec2 right_of(float heading)
        {
            return { std::cos(heading), -std::sin(heading) };
        }

        float heading_towards(glm::vec2 from, glm::vec2 to, float otherwise)
        {
            return glm::distance(from, to) > 0.01f ? std::atan2(to.x - from.x, to.y - from.y) : otherwise;
        }

        /// A point out of the park on the compass at `angle`, at flying height.
        glm::vec3 outside(const BirdGround& ground, float angle)
        {
            const glm::vec2 middle = (ground.low + ground.high) * 0.5f;
            const float reach = glm::distance(ground.low, ground.high) * 0.5f + kRates.beyond.value;
            const glm::vec2 at = middle + forward_of(angle) * reach;
            return { at.x, flying_height(ground, at), at.y };
        }

        float random_angle(random::RandomState& dice)
        {
            return static_cast<float>(random::unit(dice) * 2.0 * std::numbers::pi);
        }

        /// A point picked in a disc of radius `radius` about the origin.
        glm::vec2 in_disc(random::RandomState& dice, float radius)
        {
            const float turn = random_angle(dice);
            return forward_of(turn) * radius * std::sqrt(static_cast<float>(random::unit(dice)));
        }

        WildBirds& birds_of(SimWorld& world)
        {
            if (auto* birds = world.registry.ctx().find<WildBirds>())
                return *birds;
            return world.registry.ctx().emplace<WildBirds>(WildBirds { .dice = random::stream(world.seed, random::RandomStream::Effects) });
        }

        entt::entity make_bird(SimWorld& world, const WildBird& bird, glm::vec3 at, float heading)
        {
            const entt::entity e = world.registry.create();
            world.registry.emplace<Pose>(e, Pose { .position = at, .heading = { heading }, .was = at, .was_heading = { heading } });
            world.registry.emplace<WildBird>(e, bird);
            return e;
        }

        /// Where a gull `bird` goes next: to some litter, if there is any and it takes to it, else out of the park.
        void choose_gull_goal(SimWorld& world, const BirdGround& ground, random::RandomState& dice, WildBird& bird)
        {
            const auto litter = world.registry.view<const Litter>();
            const double wants = bird.visits == 0 ? std::min(1.0, static_cast<double>(litter.size()) / kLitterSure) : kRates.gull_again.value;
            if (bird.visits < kRates.gull_visits && !litter.empty() && random::chance(dice, wants))
            {
                std::size_t pick = random::below(dice, litter.size());
                for (const auto [e, piece] : litter.each())
                    if (pick-- == 0)
                    {
                        bird.goal = world.paths.centre(piece.node);
                        bird.leaving = false;
                        return;
                    }
            }
            bird.goal = outside(ground, random_angle(dice));
            bird.leaving = true;
        }

        /// Moves `pose` towards `to` over the park at `pace` for `dt`, turning to face the way; true once there.
        bool move_towards(Pose& pose, glm::vec2 to, float pace, float dt)
        {
            const glm::vec2 from = flat(pose.position);
            const float left = glm::distance(from, to);
            const float step = pace * dt;
            pose.heading.value = heading_towards(from, to, pose.heading.value);
            const glm::vec2 at = left <= step ? to : from + (to - from) * (step / left);
            pose.position.x = at.x;
            pose.position.z = at.y;
            return left <= step;
        }

        /// Takes `pose` up or down towards height `to`, at most `climb` a second.
        void change_height(Pose& pose, float to, float dt)
        {
            const float most = kRates.climb.value * dt;
            pose.position.y += std::clamp(to - pose.position.y, -most, most);
        }

        float eased(float t)
        {
            return t * t * (3.0f - 2.0f * t);
        }
    } // namespace

    std::size_t add_ducks(SimWorld& world, const BirdGround& ground, std::size_t count)
    {
        WildBirds& state = birds_of(world);
        const auto spot = ground.water_spot ? ground.water_spot(state.dice) : std::nullopt;
        if (!spot || count == 0)
            return 0;
        const glm::vec3 goal { spot->x, surface_at(ground, *spot), spot->y };
        const glm::vec3 start = outside(ground, random_angle(state.dice));
        const float heading = heading_towards(flat(start), flat(goal), 0.0f);
        entt::entity leader = entt::null;
        for (std::size_t i = 0; i < count; ++i)
        {
            const float rank = static_cast<float>((i + 1) / 2);
            const glm::vec2 slot { (i % 2 == 1 ? -1.0f : 1.0f) * rank * kRates.side.value, rank * kRates.behind.value };
            const glm::vec2 at = flat(start) + right_of(heading) * slot.x - forward_of(heading) * slot.y;
            const entt::entity e = make_bird(world, { .kind = BirdKind::Duck, .palette = static_cast<std::uint8_t>(random::below(state.dice, 4)), .goal = goal, .leader = leader, .slot = slot }, { at.x, start.y, at.y }, heading);
            if (leader == entt::null)
            {
                leader = e;
                world.registry.get<WildBird>(e).leader = e;
            }
        }
        return count;
    }

    std::size_t add_gulls(SimWorld& world, const BirdGround& ground, std::size_t count)
    {
        WildBirds& state = birds_of(world);
        const glm::vec3 start = outside(ground, random_angle(state.dice));
        for (std::size_t i = 0; i < count; ++i)
        {
            WildBird bird { .kind = BirdKind::Gull, .palette = static_cast<std::uint8_t>(random::below(state.dice, 4)) };
            choose_gull_goal(world, ground, state.dice, bird);
            const glm::vec2 at = flat(start) + in_disc(state.dice, 3.0f);
            const float heading = heading_towards(at, flat(bird.goal), 0.0f);
            const entt::entity e = make_bird(world, bird, { at.x, start.y, at.y }, heading);
            world.registry.get<WildBird>(e).leader = e;
        }
        return count;
    }

    std::size_t remove_wild_birds(SimWorld& world)
    {
        const auto birds = world.registry.view<WildBird>();
        const std::size_t count = birds.size();
        world.registry.destroy(birds.begin(), birds.end());
        return count;
    }

    void step_wild_birds(SimWorld& world, const BirdGround& ground, units::Seconds dt)
    {
        auto& r = world.registry;
        WildBirds& state = birds_of(world);

        // ----- 1. birds come in -----
        if (state.automatic)
        {
            std::int32_t flocks = 0, gulls = 0;
            for (const auto [e, bird] : r.view<const WildBird>().each())
            {
                flocks += bird.kind == BirdKind::Duck && bird.leader == e;
                gulls += bird.kind == BirdKind::Gull;
            }
            if ((state.duck_in -= dt) <= units::Seconds { })
            {
                state.duck_in = { static_cast<float>(random::between(state.dice, static_cast<double>(kRates.flocks_every_least.value), static_cast<double>(kRates.flocks_every_most.value))) };
                if (flocks < kRates.flocks_at_once)
                    add_ducks(world, ground, static_cast<std::size_t>(random::between(state.dice, std::int64_t { kRates.flock_least }, std::int64_t { kRates.flock_most })));
            }
            if ((state.gull_in -= dt) <= units::Seconds { })
            {
                const auto litter = static_cast<float>(r.view<const Litter>().size());
                state.gull_in = { kRates.gull_spacing.value * (kLeastGullWait + kGullWait / std::sqrt(litter + 1.0f)) };
                if (gulls < kRates.gulls_at_once)
                    add_gulls(world, ground, 1);
            }
        }

        // ----- 2. guests close by frighten birds on the ground and water -----
        std::vector<glm::vec2> guests;
        for (const auto [g, visitor, pose] : r.view<const Visitor, const Pose>(entt::exclude<Left>).each())
            guests.push_back(flat(pose.position));
        const auto frightened = [&](glm::vec3 at, float reach) { return std::ranges::any_of(guests, [&](glm::vec2 g) { return glm::distance(g, flat(at)) < reach; }); };

        // ----- 3. each bird, in the order they came -----
        std::vector<entt::entity> gone;
        for (const entt::entity e : sorted(r.view<const WildBird>()))
        {
            WildBird& bird = r.get<WildBird>(e);
            Pose& pose = r.get<Pose>(e);
            WildBird* lead = bird.leader != e && r.valid(bird.leader) ? r.try_get<WildBird>(bird.leader) : nullptr;
            if (lead == nullptr)
                bird.leader = e;
            const Pose* lead_pose = lead != nullptr ? &r.get<Pose>(bird.leader) : nullptr;
            const bool duck = bird.kind == BirdKind::Duck;

            switch (bird.job)
            {
                case BirdJob::Flying:
                {
                    // In the V, a duck takes its leader's goal and keeps its place by the leader's side.
                    glm::vec2 aim = flat(bird.goal);
                    float pace = kRates.flight.value;
                    if (lead != nullptr)
                    {
                        bird.goal = lead->goal;
                        bird.leaving = lead->leaving;
                        aim = flat(lead->goal);
                        if (lead->job == BirdJob::Flying)
                        {
                            aim = flat(lead_pose->position) + right_of(lead_pose->heading.value) * bird.slot.x - forward_of(lead_pose->heading.value) * bird.slot.y;
                            pace = kRates.catching_up.value;
                        }
                    }
                    const glm::vec2 goal = flat(bird.goal);
                    const float heading = pose.heading.value;
                    const bool near_place = lead_pose != nullptr && lead->job == BirdJob::Flying && glm::distance(flat(pose.position), aim) < 1.0f;
                    move_towards(pose, aim, pace, dt.value);
                    if (near_place)
                        pose.heading.value = lead_pose->heading.value;
                    else if (lead != nullptr && lead->job != BirdJob::Flying)
                        pose.heading.value = heading_towards(flat(pose.position), aim, heading);
                    change_height(pose, flying_height(ground, flat(pose.position)), dt.value);
                    const float to_goal = glm::distance(flat(pose.position), goal);
                    if (bird.leaving && to_goal <= kRates.arrived.value)
                        gone.push_back(e);
                    else if (!bird.leaving && to_goal <= kRates.approach.value)
                    {
                        // Over the water a duck comes down by its leader, not on the very same spot.
                        glm::vec2 spot = goal;
                        if (lead != nullptr)
                        {
                            const glm::vec2 about = lead->job == BirdJob::Flying || lead->job == BirdJob::TakingOff ? flat(lead->goal) : flat(lead_pose->position);
                            for (std::int32_t i = 0; i < kRates.tries; ++i)
                            {
                                const glm::vec2 try_at = about + in_disc(state.dice, kRates.huddle.value);
                                if (over_water(ground, try_at))
                                {
                                    spot = try_at;
                                    break;
                                }
                                spot = about;
                            }
                        }
                        bird.goal = { spot.x, surface_at(ground, spot), spot.y };
                        bird.from = pose.position;
                        bird.busy = { };
                        bird.job = BirdJob::Landing;
                    }
                    break;
                }
                case BirdJob::Landing:
                {
                    bird.busy += dt;
                    const float t = std::min(1.0f, bird.busy.value / kRates.landing.value);
                    const glm::vec2 at = glm::mix(flat(bird.from), flat(bird.goal), t);
                    pose.heading.value = heading_towards(flat(pose.position), flat(bird.goal), pose.heading.value);
                    pose.position = { at.x, glm::mix(bird.from.y, bird.goal.y, eased(t)), at.y };
                    if (t >= 1.0f)
                    {
                        bird.job = BirdJob::Resting;
                        bird.busy = duck ? units::Seconds { static_cast<float>(random::between(state.dice, static_cast<double>(kRates.stay_least.value), static_cast<double>(kRates.stay_most.value))) } : units::Seconds { static_cast<float>(random::between(state.dice, static_cast<double>(kRates.gull_stands_least.value), static_cast<double>(kRates.gull_stands_most.value))) };
                    }
                    break;
                }
                case BirdJob::Resting:
                {
                    // The rest of the flock goes when its leader does.
                    if (lead != nullptr && (lead->job == BirdJob::TakingOff || lead->job == BirdJob::Flying))
                    {
                        bird.from = pose.position;
                        bird.goal = lead->goal;
                        bird.leaving = lead->leaving;
                        bird.busy = { };
                        bird.job = BirdJob::TakingOff;
                        break;
                    }
                    // A guest coming close sends the flock off at once.
                    if (frightened(pose.position, duck ? kRates.duck_scare.value : kRates.gull_scare.value))
                    {
                        if (lead != nullptr)
                            lead->busy = { };
                        else
                            bird.busy = { };
                    }
                    if (lead == nullptr && (bird.busy -= dt) <= units::Seconds { })
                    {
                        bird.from = pose.position;
                        bird.busy = { };
                        bird.job = BirdJob::TakingOff;
                        const auto spot = duck && random::chance(state.dice, kRates.hop.value) && ground.water_spot ? ground.water_spot(state.dice) : std::nullopt;
                        if (duck && spot && glm::distance(*spot, flat(pose.position)) >= kRates.hop_least.value)
                        {
                            bird.goal = { spot->x, surface_at(ground, *spot), spot->y };
                            bird.leaving = false;
                        }
                        else if (duck)
                        {
                            bird.goal = outside(ground, random_angle(state.dice));
                            bird.leaving = true;
                        }
                        else
                        {
                            ++bird.visits;
                            choose_gull_goal(world, ground, state.dice, bird);
                        }
                        break;
                    }
                    if (!duck)
                        break;
                    // Paddling about: the leader a few steps at a time, the rest near it, always over water.
                    if (glm::distance(flat(pose.position), flat(bird.goal)) < 0.3f || !over_water(ground, flat(bird.goal)))
                    {
                        const glm::vec2 about = flat(lead != nullptr ? lead_pose->position : pose.position);
                        const float reach = lead != nullptr ? kRates.huddle.value : kRates.wander.value;
                        for (std::int32_t i = 0; i < kRates.tries; ++i)
                        {
                            const glm::vec2 try_at = about + in_disc(state.dice, reach);
                            if (over_water(ground, try_at) && over_water(ground, (try_at + flat(pose.position)) * 0.5f))
                            {
                                bird.goal = { try_at.x, surface_at(ground, try_at), try_at.y };
                                break;
                            }
                        }
                    }
                    const glm::vec2 here = flat(pose.position);
                    const glm::vec2 toward = flat(bird.goal) - here;
                    const glm::vec2 next = here + (glm::length(toward) > 0.001f ? glm::normalize(toward) * std::min(glm::length(toward), kRates.paddle.value * dt.value) : glm::vec2 { 0.0f });
                    if (over_water(ground, next))
                    {
                        pose.heading.value = heading_towards(here, next, pose.heading.value);
                        pose.position = { next.x, surface_at(ground, next), next.y };
                    }
                    else
                        bird.goal = pose.position;
                    break;
                }
                case BirdJob::TakingOff:
                {
                    bird.busy += dt;
                    const float t = std::min(1.0f, bird.busy.value / kRates.taking_off.value);
                    move_towards(pose, flat(bird.goal), kRates.flight.value * 0.5f, dt.value);
                    pose.position.y = glm::mix(bird.from.y, flying_height(ground, flat(pose.position)), eased(t));
                    if (t >= 1.0f)
                    {
                        bird.job = BirdJob::Flying;
                        bird.busy = { };
                    }
                    break;
                }
            }
        }
        for (const entt::entity e : gone)
            r.destroy(e);
    }

} // namespace rct3::simulation
