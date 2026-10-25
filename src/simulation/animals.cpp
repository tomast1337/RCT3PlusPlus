// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/animals.hpp"

#include "simulation/components.hpp"
#include "simulation/staff_care.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace rct3::simulation
{

    namespace
    {
        constexpr const AnimalRates& kRates = kAnimalRates;

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
        std::optional<glm::vec3> stroll_spot(const entt::registry& r, const AnimalGround& ground, std::int32_t enclosure, random::RandomState& dice)
        {
            if (!ground.somewhere)
                return std::nullopt;
            const auto houses = r.view<const AnimalHouse>();
            for (int i = 0; i < kRates.stroll_tries; ++i)
            {
                const auto spot = ground.somewhere(enclosure, dice);
                if (!spot)
                    return std::nullopt;
                const auto on = [&](entt::entity h) {
                    const AnimalHouse& house = houses.get<const AnimalHouse>(h);
                    return glm::all(glm::greaterThanEqual(*spot, house.low)) && glm::all(glm::lessThanEqual(*spot, house.high));
                };
                if (std::ranges::none_of(houses, on))
                    return glm::vec3 { spot->x, 0.0f, spot->y };
            }
            return std::nullopt;
        }

        Dice dice_of(const SimWorld& world, entt::entity e)
        {
            std::uint64_t mix = world.seed.value ^ (std::uint64_t { entt::to_integral(e) } << 32);
            return { random::seeded(random::Seed { random::split_mix(mix) }) };
        }

        float idle_seconds(random::RandomState& dice)
        {
            return static_cast<float>(random::between(dice, 1.0, static_cast<double>(kRates.idle.value)));
        }

        /// A spot an escaped animal at `at` wanders to, anywhere near.
        glm::vec3 roam_spot(glm::vec3 at, random::RandomState& dice)
        {
            const auto off = [&] { return static_cast<float>(random::between(dice, -static_cast<double>(kRates.roam), static_cast<double>(kRates.roam))); };
            return { at.x + off(), 0.0f, at.z + off() };
        }

        void rise(Share& need, Seconds dt, Seconds full)
        {
            need.value = std::min(1.0f, need.value + dt.value / full.value);
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
        r.emplace<Dice>(keeper, dice_of(world, keeper));
        return keeper;
    }

    entt::entity add_animal(SimWorld& world, const Animal& animal, glm::vec3 at)
    {
        auto& r = world.registry;
        const entt::entity e = r.create();
        Animal a = animal;
        a.job = AnimalJob::Crated;
        a.busy = kRates.crated;
        a.dung_in = kRates.dung;
        r.emplace<Animal>(e, std::move(a));
        r.emplace<Pose>(e, Pose { .position = at, .was = at });
        r.emplace<Dice>(e, dice_of(world, e));
        return e;
    }

    void step_animals(SimWorld& world, const AnimalGround& ground, Seconds dt, ParkMessages& messages)
    {
        auto& r = world.registry;
        const auto houses = r.view<AnimalHouse>();
        for (auto [e, house] : houses.each())
            house.since_fed.value += dt.value;
        // The house in `enclosure` nearest `at`, null for none.
        const auto house_for = [&](std::int32_t enclosure, glm::vec3 at) {
            entt::entity best = entt::null;
            float nearest = 0;
            for (const auto [h, house] : houses.each())
            {
                const float d = glm::distance(house.troughs, at);
                if (house.enclosure == enclosure && (best == entt::null || d < nearest))
                {
                    best = h;
                    nearest = d;
                }
            }
            return best;
        };
        std::vector<std::int32_t> dung_in; // By enclosure
        for (const auto [d, dung] : r.view<const Dung>().each())
        {
            if (dung.enclosure >= 0)
            {
                dung_in.resize(std::max(dung_in.size(), static_cast<std::size_t>(dung.enclosure) + 1));
                ++dung_in[static_cast<std::size_t>(dung.enclosure)];
            }
        }
        const auto dung_count = [&](std::int32_t enclosure) { return enclosure >= 0 && static_cast<std::size_t>(enclosure) < dung_in.size() ? dung_in[static_cast<std::size_t>(enclosure)] : 0; };

        // The animals.
        std::vector<std::pair<glm::vec3, std::int32_t>> dropped; // Dung left this step, and its enclosure
        const auto animals = r.view<Animal, Pose, Dice>();
        for (auto [e, animal, pose, dice] : animals.each())
        {
            pose.was = pose.position;
            pose.was_heading = pose.heading;
            if (animal.job == AnimalJob::Crated)
            {
                animal.busy.value -= dt.value;
                if (animal.busy.value <= 0)
                    animal.job = AnimalJob::Idle;
                continue;
            }
            // Where it stands decides its enclosure; in none, it is out.
            const std::int32_t standing = ground.enclosure ? ground.enclosure({ pose.position.x, pose.position.z }) : animal.enclosure;
            if (standing < 0 && !animal.escaped)
                post(messages, { .text_key = std::string(kAnimalEscaped), .subject_name = animal.name });
            animal.escaped = standing < 0;
            animal.enclosure = standing;
            rise(animal.hunger, dt, kRates.hungry);
            rise(animal.thirst, dt, kRates.thirsty);
            if (animal.job != AnimalJob::Sleeping)
                rise(animal.tiredness, dt, kRates.sleepy);
            const bool company = std::ranges::any_of(animals, [&](entt::entity o) {
                const Animal& other = animals.get<Animal>(o);
                return o != e && other.enclosure == animal.enclosure && other.species == animal.species && other.job != AnimalJob::Crated;
            });
            if (company)
                animal.loneliness.value = std::max(0.0f, animal.loneliness.value - dt.value / kRates.lonely.value);
            else
                rise(animal.loneliness, dt, kRates.lonely);
            animal.dung_in.value -= dt.value;
            if (animal.dung_in.value <= 0 && !animal.escaped)
            {
                animal.dung_in = kRates.dung;
                dropped.emplace_back(pose.position, animal.enclosure);
            }
            const bool ailing = animal.hunger.value >= 1 || animal.thirst.value >= 1 || dung_count(animal.enclosure) > kRates.dung_limit;
            if (ailing)
                animal.health.value = std::max(0.0f, animal.health.value - dt.value / kRates.sickening.value);
            else
                animal.health.value = std::min(1.0f, animal.health.value + dt.value / kRates.mending.value);

            const entt::entity home = house_for(animal.enclosure, pose.position);
            AnimalHouse* house = home != entt::null ? &houses.get<AnimalHouse>(home) : nullptr;
            switch (animal.job)
            {
                case AnimalJob::Crated:
                    break;
                case AnimalJob::Idle:
                    animal.busy.value -= dt.value;
                    if (animal.busy.value > 0)
                        break;
                    if (house != nullptr && (animal.hunger.value >= kRates.wants.value || animal.thirst.value >= kRates.wants.value) && (house->food.value > 0 || house->water.value > 0))
                    {
                        animal.job = AnimalJob::Feeding;
                        animal.goal = house->troughs;
                        animal.busy = kRates.eating;
                    }
                    else if (house != nullptr && animal.tiredness.value >= kRates.wants.value)
                    {
                        animal.job = AnimalJob::Sleeping;
                        animal.goal = house->troughs;
                        animal.busy = kRates.sleeping;
                    }
                    else if (const auto spot = animal.escaped ? std::optional(roam_spot(pose.position, dice.state)) : stroll_spot(r, ground, animal.enclosure, dice.state))
                    {
                        animal.job = AnimalJob::Strolling;
                        animal.goal = *spot;
                    }
                    else
                        animal.busy = kRates.idle;
                    break;
                case AnimalJob::Strolling:
                    if (walk_towards(pose, animal.goal, kRates.animal_pace, dt, ground))
                    {
                        animal.job = AnimalJob::Idle;
                        animal.busy.value = idle_seconds(dice.state);
                    }
                    break;
                case AnimalJob::Feeding:
                case AnimalJob::Sleeping:
                    if (!walk_towards(pose, animal.goal, kRates.animal_pace, dt, ground))
                        break;
                    animal.busy.value -= dt.value;
                    if (animal.busy.value > 0)
                        break;
                    if (animal.job == AnimalJob::Sleeping)
                        animal.tiredness = { };
                    else if (house != nullptr)
                    {
                        // What the troughs hold goes as far as it goes.
                        const auto take = [&](Share& trough, Share& need) {
                            const float had = trough.value;
                            trough.value = std::max(0.0f, trough.value - kRates.meal.value);
                            if (had > 0)
                                need = { };
                        };
                        take(house->food, animal.hunger);
                        take(house->water, animal.thirst);
                    }
                    animal.job = AnimalJob::Idle;
                    animal.busy.value = idle_seconds(dice.state);
                    break;
            }
        }
        for (const auto& [at, enclosure] : dropped)
        {
            const entt::entity d = r.create();
            r.emplace<Dung>(d, Dung { enclosure });
            r.emplace<Pose>(d, Pose { .position = at, .was = at });
        }

        // The keepers.
        for (auto [e, duty, pose, dice] : r.view<KeeperDuty, Pose, Dice>().each())
        {
            pose.was = pose.position;
            pose.was_heading = pose.heading;
            AnimalHouse* house = r.valid(duty.house) ? r.try_get<AnimalHouse>(duty.house) : nullptr;
            if (house == nullptr)
                continue;
            switch (duty.job)
            {
                case KeeperJob::Idle:
                {
                    duty.busy.value -= dt.value;
                    if (duty.busy.value > 0)
                        break;
                    // An escaped animal first, the nearest no other keeper is after; then the troughs when due or
                    // running low; then the dung nearest.
                    entt::entity loose = entt::null;
                    for (const auto [a, animal, at] : r.view<const Animal, const Pose>().each())
                    {
                        const bool chased = std::ranges::any_of(r.view<const KeeperDuty>(), [&](entt::entity k) { return k != e && r.get<const KeeperDuty>(k).target == a; });
                        if (animal.escaped && animal.job != AnimalJob::Crated && !chased && (loose == entt::null || glm::distance(at.position, pose.position) < glm::distance(r.get<Pose>(loose).position, pose.position)))
                            loose = a;
                    }
                    if (loose != entt::null)
                    {
                        // shortcut: the keeper walks up and crates the animal; the darts from the air and the
                        // player carrying the crate home come once the pick-up hand can lift crates.
                        duty.job = KeeperJob::Catching;
                        duty.target = loose;
                        duty.busy = kRates.catching;
                        break;
                    }
                    if (house->since_fed.value >= kRates.feeding.value || house->food.value <= kRates.refill.value || house->water.value <= kRates.refill.value)
                    {
                        duty.job = KeeperJob::Filling;
                        duty.goal = house->troughs;
                        duty.busy = kRates.filling;
                        break;
                    }
                    entt::entity nearest = entt::null;
                    for (const auto [d, dung, at] : r.view<const Dung, const Pose>().each())
                        if (dung.enclosure == house->enclosure && (nearest == entt::null || glm::distance(at.position, pose.position) < glm::distance(r.get<Pose>(nearest).position, pose.position)))
                            nearest = d;
                    if (nearest != entt::null)
                    {
                        duty.job = KeeperJob::Clearing;
                        duty.target = nearest;
                        duty.goal = r.get<Pose>(nearest).position;
                        duty.busy = kRates.clearing;
                    }
                    else if (const auto spot = stroll_spot(r, ground, house->enclosure, dice.state))
                    {
                        duty.job = KeeperJob::Strolling;
                        duty.goal = *spot;
                    }
                    else
                        duty.busy = kRates.idle;
                    break;
                }
                case KeeperJob::Strolling:
                    if (walk_towards(pose, duty.goal, kRates.keeper_pace, dt, ground))
                    {
                        duty.job = KeeperJob::Idle;
                        duty.busy.value = idle_seconds(dice.state);
                    }
                    break;
                case KeeperJob::Catching:
                {
                    Animal* animal = r.valid(duty.target) ? r.try_get<Animal>(duty.target) : nullptr;
                    if (animal == nullptr)
                    {
                        duty.job = KeeperJob::Idle;
                        duty.target = entt::null;
                        break;
                    }
                    Pose& caught = r.get<Pose>(duty.target);
                    if (animal->job != AnimalJob::Crated)
                    {
                        // After it till close enough, then into a crate where it stands, long enough to be carried home.
                        if (!walk_towards(pose, caught.position, kRates.keeper_pace, dt, ground))
                            break;
                        animal->job = AnimalJob::Crated;
                        animal->busy.value = kRates.catching.value + kRates.crated.value;
                        break;
                    }
                    duty.busy.value -= dt.value;
                    if (duty.busy.value > 0)
                        break;
                    const glm::vec3 home = stroll_spot(r, ground, house->enclosure, dice.state).value_or(house->troughs);
                    const glm::vec2 flat { home.x, home.z };
                    caught.position = caught.was = { home.x, ground.height ? ground.height(flat) : home.y, home.z };
                    animal->enclosure = house->enclosure;
                    animal->escaped = false;
                    animal->busy = kRates.crated;
                    duty.target = entt::null;
                    duty.job = KeeperJob::Idle;
                    duty.busy.value = idle_seconds(dice.state);
                    break;
                }
                case KeeperJob::Filling:
                case KeeperJob::Clearing:
                    // Another keeper may have cleared the dung on the way.
                    if (duty.job == KeeperJob::Clearing && !r.valid(duty.target))
                    {
                        duty.job = KeeperJob::Idle;
                        break;
                    }
                    if (!walk_towards(pose, duty.goal, kRates.keeper_pace, dt, ground))
                        break;
                    duty.busy.value -= dt.value;
                    if (duty.busy.value > 0)
                        break;
                    if (duty.job == KeeperJob::Filling)
                    {
                        // shortcut: the food costs nothing; charge each filling once the house's window sets the food.
                        house->food = { 1 };
                        house->water = { 1 };
                        house->since_fed = { };
                    }
                    else
                        r.destroy(duty.target);
                    duty.target = entt::null;
                    duty.job = KeeperJob::Idle;
                    duty.busy.value = idle_seconds(dice.state);
                    break;
            }
        }
    }

} // namespace rct3::simulation
