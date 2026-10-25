// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/animals.hpp"

#include "simulation/components.hpp"
#include "simulation/staff_care.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::simulation
{

    namespace
    {
        /// Takes `pose` towards `goal` at `pace` for `dt`, over the ground; true once there.
        bool walk_towards(Pose& pose, glm::vec3 goal, float pace, Seconds dt, const AnimalGround& ground)
        {
            const glm::vec2 from { pose.position.x, pose.position.z };
            const glm::vec2 to { goal.x, goal.z };
            const float left = glm::distance(from, to);
            const float step = pace * dt.value;
            const glm::vec2 at = left <= step ? to : from + (to - from) * (step / left);
            if (left > 0)
                pose.heading.value = std::atan2(to.x - from.x, to.y - from.y);
            pose.position = { at.x, ground.height ? ground.height(at) : pose.position.y, at.y };
            pose.walked.value += glm::distance(from, at);
            return left <= step;
        }
        /// A spot to stroll to in `enclosure`, off the houses; nothing when none turns up.
        std::optional<glm::vec2> stroll_spot(const entt::registry& r, const AnimalGround& ground, std::int32_t enclosure, random::RandomState& dice)
        {
            if (!ground.somewhere)
                return std::nullopt;
            for (int i = 0; i < kStrollTries; ++i)
            {
                const auto spot = ground.somewhere(enclosure, dice);
                if (!spot)
                    return std::nullopt;
                const auto on = [&](const AnimalHouse& h) { return glm::all(glm::greaterThanEqual(*spot, h.low)) && glm::all(glm::lessThanEqual(*spot, h.high)); };
                const auto houses = r.view<const AnimalHouse>();
                if (std::ranges::none_of(houses, [&](entt::entity h) { return on(houses.get<const AnimalHouse>(h)); }))
                    return spot;
            }
            return std::nullopt;
        }
    } // namespace

    entt::entity add_animal_house(SimWorld& world, const AnimalHouse& house, glm::vec3 at)
    {
        auto& r = world.registry;
        const entt::entity home = r.create();
        r.emplace<AnimalHouse>(home, house);
        const entt::entity keeper = r.create();
        Staff staff { .role = StaffRole::Keeper };
        set_wage_level(staff, staff.wage_level); // Paid as other new staff are
        r.emplace<Staff>(keeper, staff);
        r.emplace<Pose>(keeper, Pose { .position = at, .was = at });
        r.emplace<KeeperDuty>(keeper, KeeperDuty { .house = home });
        std::uint64_t mix = world.seed.value ^ (std::uint64_t { entt::to_integral(keeper) } << 32);
        r.emplace<Dice>(keeper, Dice { random::seeded(random::Seed { random::split_mix(mix) }) });
        return keeper;
    }

    void step_animals(SimWorld& world, const AnimalGround& ground, Seconds dt)
    {
        auto& r = world.registry;
        for (auto [e, work, pose, dice] : r.view<KeeperDuty, Pose, Dice>().each())
        {
            pose.was = pose.position;
            pose.was_heading = pose.heading;
            const auto* house = r.valid(work.house) ? r.try_get<AnimalHouse>(work.house) : nullptr;
            if (house == nullptr)
                continue;
            switch (work.job)
            {
                case KeeperJob::Idle:
                    work.busy.value -= dt.value;
                    if (work.busy.value > 0)
                        break;
                    if (const auto spot = stroll_spot(r, ground, house->enclosure, dice.state))
                    {
                        work.goal = { spot->x, 0.0f, spot->y };
                        work.job = KeeperJob::Walking;
                    }
                    else
                        work.busy.value = kKeeperIdleSeconds;
                    break;
                case KeeperJob::Walking:
                    if (walk_towards(pose, work.goal, kKeeperPace, dt, ground))
                    {
                        work.job = KeeperJob::Idle;
                        work.busy.value = static_cast<float>(random::between(dice.state, 1.0, static_cast<double>(kKeeperIdleSeconds)));
                    }
                    break;
            }
        }
    }

} // namespace rct3::simulation
