// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/animals.hpp"

#include "simulation/components.hpp"
#include "simulation/staff/care.hpp"
#include "simulation/staff/patrol.hpp"
#include "simulation/systems.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace rct3::simulation
{

    namespace
    {
        constexpr const AnimalRates& kRates = kAnimalRates;

        /// Takes `pose` towards `goal` at `pace` for `dt`, over the ground; true once there.
        bool walk_towards(Pose& pose, glm::vec3 goal, units::Pace pace, units::Seconds dt, const AnimalGround& ground)
        {
            const glm::vec2 from { pose.position.x, pose.position.z };
            const glm::vec2 to { goal.x, goal.z };
            const float left = glm::distance(from, to);
            const float step = pace.value * dt.value;
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

        void rise(units::Share& need, units::Seconds dt, units::Seconds full)
        {
            need.value = std::min(1.0f, need.value + dt.value / full.value);
        }
    } // namespace

    Dice dice_for(const SimWorld& world, entt::entity e)
    {
        std::uint64_t mix = world.seed.value ^ (std::uint64_t { entt::to_integral(e) } << 32);
        return { random::seeded(random::Seed { random::split_mix(mix) }) };
    }

    entt::entity add_animal_house(SimWorld& world, const AnimalHouse& house, glm::vec3 at)
    {
        const entt::entity home = world.registry.create();
        world.registry.emplace<AnimalHouse>(home, house);
        return add_keeper(world, home, at);
    }

    entt::entity add_keeper(SimWorld& world, entt::entity home, glm::vec3 at)
    {
        auto& r = world.registry;
        const entt::entity keeper = r.create();
        Staff staff { .role = StaffRole::Keeper, .tasks = kClearDungTask | kFillTroughsTask };
        staff::set_wage_level(staff, staff.wage_level); // Paid as other new staff are
        r.emplace<Staff>(keeper, staff);
        r.emplace<Pose>(keeper, Pose { .position = at, .was = at });
        r.emplace<KeeperDuty>(keeper, KeeperDuty { .house = home });
        r.emplace<Dice>(keeper, dice_for(world, keeper));
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
        r.emplace<Dice>(e, dice_for(world, e));
        return e;
    }

    void import_animal_houses(SimWorld& world, std::span<const AnimalHouse> houses, std::span<const std::pair<entt::entity, std::int32_t>> keepers)
    {
        auto& r = world.registry;
        std::vector<entt::entity> homes;
        for (const AnimalHouse& house : houses)
        {
            homes.push_back(r.create());
            r.emplace<AnimalHouse>(homes.back(), house);
        }
        std::vector<bool> kept(houses.size(), false);
        for (const auto& [keeper, enclosure] : keepers)
        {
            if (!r.valid(keeper) || !r.all_of<Pose>(keeper))
                continue;
            const glm::vec3 at = r.get<Pose>(keeper).position;
            std::optional<std::size_t> best;
            const auto better = [&](std::size_t i) { return !best || (kept[i] != kept[*best] ? !kept[i] : glm::distance(houses[i].troughs, at) < glm::distance(houses[*best].troughs, at)); };
            for (std::size_t i = 0; i < houses.size(); ++i)
                if (houses[i].enclosure == enclosure && better(i))
                    best = i;
            if (!best)
                continue;
            kept[*best] = true;
            r.emplace_or_replace<KeeperDuty>(keeper, KeeperDuty { .house = homes[*best] });
            r.emplace_or_replace<Dice>(keeper, dice_for(world, keeper));
        }
    }

    void import_animals(SimWorld& world, std::span<const save::model::ZooAnimal> saved, const std::function<std::int32_t(glm::vec2)>& enclosure)
    {
        for (const save::model::ZooAnimal& z : saved)
        {
            // Its model's place: rows of the x, y and z axes, then the position.
            const glm::vec3 at { z.world[12], z.world[13], z.world[14] };
            const std::int32_t in = enclosure ? enclosure({ at.x, at.z }) : -1;
            // The park keeps how fed, watered and in company it is, where we keep how much it wants each.
            const auto share = [](float v) { return units::Share { std::clamp(v, 0.0f, 1.0f) }; };
            const entt::entity e = add_animal(world, { .species = z.species, .name = z.first_names, .adult = z.is_adult, .male = z.is_male, .enclosure = in, .hunger = share(1 - z.food), .thirst = share(1 - z.drink), .tiredness = share(z.tiredness), .loneliness = share(1 - z.social), .health = share(z.health) }, at);
            auto& r = world.registry;
            r.get<Pose>(e).heading.value = r.get<Pose>(e).was_heading.value = std::atan2(z.world[8], z.world[10]);
            Animal& animal = r.get<Animal>(e);
            if (!z.in_crate)
            {
                animal.job = AnimalJob::Idle;
                animal.busy = { };
            }
            animal.escaped = in < 0 && !z.in_crate;
        }
    }

    void step_animals(SimWorld& world, const AnimalGround& ground, units::Seconds dt, park::ParkMessages& messages)
    {
        auto& r = world.registry;
        const float now = static_cast<float>(world.tick) * kTickSeconds;
        // The park's inspector, who tells the player what is wrong; null when it has none.
        const auto workers = r.view<const Staff>();
        const auto inspecting = std::ranges::find_if(workers, [&](entt::entity w) { return workers.get<const Staff>(w).role == StaffRole::Inspector; });
        const entt::entity inspector = inspecting != workers.end() ? *inspecting : entt::null;
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
        // Dung by enclosure, less what the animals living there put up with.
        std::vector<std::int32_t> dung_in;
        const auto count_in = [&](std::int32_t enclosure, std::int32_t n) {
            if (enclosure < 0)
                return;
            dung_in.resize(std::max(dung_in.size(), static_cast<std::size_t>(enclosure) + 1));
            dung_in[static_cast<std::size_t>(enclosure)] += n;
        };
        for (const auto [a, animal] : r.view<const Animal>().each())
            count_in(animal.enclosure, -kRates.dung_limit);
        for (const auto [d, dung] : r.view<const Dung>().each())
            count_in(dung.enclosure, 1);
        const auto dung_over = [&](std::int32_t enclosure) { return enclosure >= 0 && static_cast<std::size_t>(enclosure) < dung_in.size() ? dung_in[static_cast<std::size_t>(enclosure)] : 0; };

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
                park::post(messages, { .text_key = std::string(kAnimalEscaped), .subject_name = animal.name });
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
            const bool ailing = animal.hunger.value >= 1 || animal.thirst.value >= 1 || dung_over(animal.enclosure) > 0;
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
                    if (house != nullptr && animal.hunger.value >= kRates.wants.value && house->food.value <= 0 && ++animal.empty_troughs >= kRates.empty_tries)
                    {
                        animal.empty_troughs = 0;
                        if (inspector != entt::null)
                            tell(r, inspector, Finding::TroughsEmpty, e, now);
                    }
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
                        const auto take = [&](units::Share& trough, units::Share& need) {
                            const float had = trough.value;
                            trough.value = std::max(0.0f, trough.value - kRates.meal.value);
                            if (had > 0)
                                need = { };
                        };
                        take(house->food, animal.hunger);
                        if (animal.hunger.value == 0)
                            animal.empty_troughs = 0;
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

        // The keepers. Each keeps to the enclosure of their house and fills the troughs of any house in it, as
        // their tasks and patrol allow.
        const auto due = [&](const AnimalHouse& h) { return h.since_fed.value >= kRates.feeding.value || h.food.value <= kRates.refill.value || h.water.value <= kRates.refill.value; };
        const auto tasked = [&](entt::entity k, std::int32_t task) {
            const auto* staff = r.try_get<Staff>(k);
            return staff == nullptr || (staff->tasks & task) != 0;
        };
        const auto covers = [&](entt::entity k, glm::vec3 at) {
            const auto* patrol = r.try_get<staff::Patrol>(k);
            return patrol == nullptr || patrol->tiles.empty() || std::ranges::contains(patrol->tiles, glm::ivec2(glm::floor((glm::vec2(at.x, at.z) - world.paths.origin) / world.paths.tile_size)));
        };
        const auto keepers = r.view<KeeperDuty>();
        const auto being_filled = [&](entt::entity h) { return std::ranges::any_of(keepers, [&](entt::entity k) { return keepers.get<KeeperDuty>(k).job == KeeperJob::Filling && keepers.get<KeeperDuty>(k).target == h; }); };
        const auto enclosure_of = [&](const KeeperDuty& duty) { return r.valid(duty.house) && r.all_of<AnimalHouse>(duty.house) ? r.get<AnimalHouse>(duty.house).enclosure : -1; };
        for (auto [e, duty, pose, dice] : r.view<KeeperDuty, Pose, Dice>().each())
        {
            pose.was = pose.position;
            pose.was_heading = pose.heading;
            AnimalHouse* house = r.valid(duty.house) ? r.try_get<AnimalHouse>(duty.house) : nullptr;
            if (house == nullptr)
                continue;
            Staff* staff = r.try_get<Staff>(e);
            switch (duty.job)
            {
                case KeeperJob::Idle:
                {
                    duty.busy.value -= dt.value;
                    if (duty.busy.value > 0)
                        break;
                    // An escaped animal first, the nearest no other keeper is after; then the nearest house's troughs
                    // when due or running low; then the dung nearest.
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
                    entt::entity fill = entt::null;
                    if (tasked(e, kFillTroughsTask))
                        for (const auto [h, other] : houses.each())
                            if (other.enclosure == house->enclosure && due(other) && covers(e, other.troughs) && !being_filled(h) && (fill == entt::null || glm::distance(other.troughs, pose.position) < glm::distance(houses.get<AnimalHouse>(fill).troughs, pose.position)))
                                fill = h;
                    if (fill != entt::null)
                    {
                        duty.job = KeeperJob::Filling;
                        duty.target = fill;
                        duty.goal = houses.get<AnimalHouse>(fill).troughs;
                        duty.busy = kRates.filling;
                        break;
                    }
                    entt::entity nearest = entt::null;
                    if (tasked(e, kClearDungTask))
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
                {
                    // Another keeper may have cleared the dung on the way, or the house be pulled down.
                    AnimalHouse* filled = duty.job == KeeperJob::Filling && r.valid(duty.target) ? r.try_get<AnimalHouse>(duty.target) : nullptr;
                    if (!r.valid(duty.target) || (duty.job == KeeperJob::Filling && filled == nullptr))
                    {
                        duty.job = KeeperJob::Idle;
                        duty.target = entt::null;
                        break;
                    }
                    if (!walk_towards(pose, duty.goal, kRates.keeper_pace, dt, ground))
                        break;
                    duty.busy.value -= dt.value;
                    if (duty.busy.value > 0)
                        break;
                    if (filled != nullptr)
                    {
                        // shortcut: the food costs nothing; charge each filling once the house's window sets the food.
                        filled->food = { 1 };
                        filled->water = { 1 };
                        filled->since_fed = { };
                        if (staff != nullptr)
                            ++staff->filled;
                    }
                    else
                    {
                        r.destroy(duty.target);
                        if (staff != nullptr)
                            ++staff->cleared;
                    }
                    duty.target = entt::null;
                    duty.job = KeeperJob::Idle;
                    duty.busy.value = idle_seconds(dice.state);
                    break;
                }
            }
        }

        // What the inspector finds: a house due a filling that no keeper of its enclosure is set to fill, or whose
        // keepers' patrols all leave it out; and an enclosure with more dung than its animals put up with, told once
        // at its first house. shortcut: the game's inspector finds the dung at an enclosure's viewing gallery, which
        // parks here do not have yet; it is told of from afar while their task is on.
        if (inspector == entt::null)
            return;
        std::vector<std::int32_t> dung_told;
        for (const auto [h, house] : houses.each())
        {
            if (due(house) && !being_filled(h))
            {
                bool any = false;
                bool reach = false;
                for (const auto [k, duty] : keepers.each())
                    if (enclosure_of(duty) == house.enclosure && tasked(k, kFillTroughsTask))
                    {
                        any = true;
                        reach = reach || covers(k, house.troughs);
                    }
                if (!any)
                    tell(r, inspector, Finding::NoKeeperCanReach, h, now);
                else if (!reach)
                    tell(r, inspector, Finding::KeeperBadZones, h, now);
            }
            if ((r.get<const Staff>(inspector).tasks & kGalleriesTask) != 0 && dung_over(house.enclosure) > 0 && !std::ranges::contains(dung_told, house.enclosure))
            {
                dung_told.push_back(house.enclosure);
                tell(r, inspector, Finding::TooMuchDung, h, now);
            }
        }
    }

} // namespace rct3::simulation
