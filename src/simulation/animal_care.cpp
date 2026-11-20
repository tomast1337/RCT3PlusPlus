// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/animal_care.hpp"

#include "core/types.hpp"
#include "simulation/components.hpp"
#include "simulation/staff/care.hpp"
#include "simulation/systems.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace rct3::simulation
{

    namespace
    {

        constexpr const AnimalCareRates& kCare = kAnimalCareRates;

        bool same_kind(std::string_view a, std::string_view b)
        {
            return std::ranges::equal(a, b, [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
        }

    } // namespace

    AnimalCare care_for(const ovl::animal::SpeciesCare& species, std::string_view house, random::RandomState& dice)
    {
        AnimalCare care { .house = std::string(house), .space = species.space };
        // A species whose data gave no nature keeps the middle values.
        if (species.appetite <= 0)
            return care;
        const auto near = [&](i32 middle, i32 low) { return static_cast<i32>(std::clamp<i64>(random::between(dice, i64 { middle - kCare.spread }, i64 { middle + kCare.spread }), low, 20)); };
        care.appetite = near(species.appetite, 1);
        care.sociability = near(species.sociability, 0);
        return care;
    }

    bool eats_at(const entt::registry& r, entt::entity animal, entt::entity house)
    {
        const auto* care = r.try_get<AnimalCare>(animal);
        const auto* kind = r.try_get<HouseCare>(house);
        return care == nullptr || kind == nullptr || care->house.empty() || kind->kind.empty() || same_kind(care->house, kind->kind);
    }

    i32 wanted_company(const entt::registry& r, entt::entity animal)
    {
        const auto* care = r.try_get<AnimalCare>(animal);
        return care != nullptr ? (care->sociability + kCare.per_companion / 2) / kCare.per_companion : 1;
    }

    units::Seconds hungry_after(const entt::registry& r, entt::entity animal)
    {
        const auto* care = r.try_get<AnimalCare>(animal);
        // The middle appetite, 10, as hungry as kAnimalRates says.
        return { care != nullptr ? kAnimalRates.hungry.value * 10.0f / static_cast<f32>(std::max(care->appetite, 1)) : kAnimalRates.hungry.value };
    }

    units::Share most_health(const entt::registry& r, entt::entity animal)
    {
        const auto* care = r.try_get<AnimalCare>(animal);
        return care != nullptr ? care->most_health : units::Share { 1 };
    }

    bool reaches(const AnimalGround& ground, glm::vec3 from, glm::vec3 to)
    {
        if (!ground.height)
            return true;
        const glm::vec2 a { from.x, from.z };
        const glm::vec2 b { to.x, to.z };
        const f32 length = glm::distance(a, b);
        const i32 steps = std::max(1, static_cast<i32>(std::ceil(length / kCare.stride.value)));
        const f32 rise = kCare.steepest * length / static_cast<f32>(steps);
        f32 was = ground.height(a);
        for (i32 i = 1; i <= steps; ++i)
        {
            const f32 h = ground.height(glm::mix(a, b, static_cast<f32>(i) / static_cast<f32>(steps)));
            if (std::abs(h - was) > rise)
                return false;
            was = h;
        }
        return true;
    }

    bool welcome_at(entt::registry& r, const AnimalGround& ground, entt::entity animal, entt::entity house, entt::entity inspector, f32 now)
    {
        auto& care = r.get_or_emplace<AnimalCare>(animal);
        const glm::vec3 troughs = r.get<const AnimalHouse>(house).troughs;
        // Going without a few times in a row brings the inspector's word.
        const auto turned = [&](i32& tries, Finding finding) {
            if (++tries < kCare.tries)
                return false;
            tries = 0;
            if (inspector != entt::null)
                tell(r, inspector, finding, animal, now);
            return false;
        };
        const auto animals = r.view<const Animal>();
        const auto feeding = std::ranges::count_if(animals, [&](entt::entity o) { return o != animal && animals.get<const Animal>(o).job == AnimalJob::Feeding && animals.get<const Animal>(o).goal == troughs; });
        if (feeding >= kCare.places)
            return turned(care.turned_away, Finding::HouseFull);
        care.turned_away = 0;
        if (!reaches(ground, r.get<const Pose>(animal).position, troughs))
            return turned(care.out_of_reach, Finding::CannotReachHouse);
        care.out_of_reach = 0;
        return true;
    }

    units::Seconds at_training(units::Seconds job, i32 training)
    {
        const f32 untaught = static_cast<f32>(staff::kMostTraining - std::clamp(training, 0, staff::kMostTraining)) / static_cast<f32>(staff::kMostTraining);
        return { job.value * (1 + (kCare.untrained - 1) * untaught) };
    }

    units::Seconds job_time(const entt::registry& r, entt::entity keeper, KeeperJob job)
    {
        const auto* staff = r.try_get<Staff>(keeper);
        return at_training(job == KeeperJob::Filling ? kAnimalRates.filling : kAnimalRates.clearing, staff != nullptr ? staff->training : staff::kMostTraining);
    }

    void fetch_tool(entt::registry& r, const AnimalGround& ground, entt::entity keeper, entt::entity inspector, f32 now)
    {
        auto& duty = r.get<KeeperDuty>(keeper);
        auto& tool = r.get_or_emplace<KeeperTool>(keeper);
        // A house the care does not know of has no hut to go to.
        const auto* home = r.valid(duty.house) ? r.try_get<HouseCare>(duty.house) : nullptr;
        if (tool.holding == duty.job || home == nullptr)
            return;
        if (!home->hut || !reaches(ground, r.get<const Pose>(keeper).position, *home->hut))
        {
            if (inspector != entt::null)
                tell(r, inspector, Finding::KeeperCantReachHut, keeper, now);
            return;
        }
        tool.fetching = duty.job;
        duty.job = KeeperJob::Swapping;
        duty.goal = *home->hut;
        duty.busy = kCare.swapping;
    }

    void tool_swapped(entt::registry& r, entt::entity keeper)
    {
        auto& duty = r.get<KeeperDuty>(keeper);
        auto& tool = r.get_or_emplace<KeeperTool>(keeper);
        tool.holding = tool.fetching;
        const bool filling = tool.fetching == KeeperJob::Filling;
        if (!r.valid(duty.target) || (filling ? !r.all_of<AnimalHouse>(duty.target) : !r.all_of<Pose>(duty.target)))
        {
            duty.job = KeeperJob::Idle;
            duty.target = entt::null;
            return;
        }
        duty.job = tool.fetching;
        duty.goal = filling ? r.get<const AnimalHouse>(duty.target).troughs : r.get<const Pose>(duty.target).position;
        duty.busy = job_time(r, keeper, duty.job);
    }

    void step_animal_care(SimWorld& world, std::span<const EnclosureSite> sites, units::Seconds dt, park::ParkMessages& messages)
    {
        auto& r = world.registry;
        const f32 now = static_cast<f32>(world.tick) * kTickSeconds;
        const auto workers = r.view<const Staff>();
        const auto inspecting = std::ranges::find_if(workers, [&](entt::entity w) { return workers.get<const Staff>(w).role == StaffRole::Inspector; });
        const entt::entity inspector = inspecting != workers.end() ? *inspecting : entt::null;
        // Animals from before care was kept, or added without it, are cared for as any.
        std::vector<entt::entity> uncared;
        for (const entt::entity e : r.view<const Animal>(entt::exclude<AnimalCare>))
            uncared.push_back(e);
        for (const entt::entity e : uncared)
            r.emplace<AnimalCare>(e);

        // The room each enclosure's animals want, and how many there are of each species, out of their crates.
        const auto settled = [](const Animal& a) { return a.enclosure && a.job != AnimalJob::Crated; };
        std::map<std::pair<i32, std::string>, std::pair<i32, i32>> kinds; // Most space and head count, by enclosure and species
        std::map<i32, i32> heads;
        const auto animals = r.view<Animal, AnimalCare>();
        for (const auto [e, animal, care] : animals.each())
            if (settled(animal))
            {
                auto& [space, count] = kinds[{ *animal.enclosure, animal.species }];
                space = std::max(space, care.space);
                ++count;
                ++heads[*animal.enclosure];
            }
        std::map<i32, f32> wanted;
        for (const auto& [at, kind] : kinds)
            wanted[at.first] += static_cast<f32>(kind.first) * (1 + kCare.more_room.value * static_cast<f32>(kind.second - 1));

        std::vector<i32> told; // Enclosures told of
        std::vector<entt::entity> taken;
        for (const auto [e, animal, care] : animals.each())
        {
            if (animal.job == AnimalJob::Crated)
                continue;
            if (settled(animal))
            {
                const i32 in = *animal.enclosure;
                const EnclosureSite site = static_cast<usize>(in) < sites.size() ? sites[static_cast<usize>(in)] : EnclosureSite { };
                const f32 want = wanted[in];
                const f32 room = want > 0 ? std::min(1.0f, static_cast<f32>(site.tiles) / want) : 1.0f;
                const f32 planted = site.tiles > 0 ? std::min(1.0f, static_cast<f32>(site.plants) / (static_cast<f32>(site.tiles) * kCare.plants_per_tile)) : 0.0f;
                care.habitat = { (1 - kCare.planting.value) * room + kCare.planting.value * planted };
                const i32 others = heads[in] - kinds[{ in, animal.species }].second;
                care.annoyed = wanted_company(r, e) <= 1 && others > kCare.crowd;
                const bool small = site.tiles < care.space;
                if (inspector != entt::null && site.tiles > 0 && (small || static_cast<f32>(site.tiles) < want) && !std::ranges::contains(told, in))
                {
                    told.push_back(in);
                    tell(r, inspector, small ? Finding::EnclosureTooSmall : Finding::EnclosureOvercrowded, e, now);
                }
            }
            care.most_health = { std::max(0.0f, 1 - kCare.habitat_weight.value * (1 - care.habitat.value) - kCare.lonely_weight.value * animal.loneliness.value - (care.annoyed ? kCare.annoyed_weight.value : 0)) };
            if (inspector != entt::null)
            {
                if (animal.loneliness.value >= kCare.lonely_told.value)
                    tell(r, inspector, Finding::AnimalLonely, e, now);
                if (care.annoyed)
                    tell(r, inspector, Finding::AnimalAntiSocial, e, now);
                const auto houses = r.view<const AnimalHouse>();
                if (settled(animal) && animal.hunger.value >= kAnimalRates.wants.value && std::ranges::none_of(houses, [&](entt::entity h) { return houses.get<const AnimalHouse>(h).enclosure == animal.enclosure && eats_at(r, e, h); }))
                    tell(r, inspector, Finding::AnimalNeedsHouse, e, now);
            }

            // Neglect: a warning, then the animal taken away.
            if (animal.health.value >= kCare.neglect.value)
            {
                care.neglected = { };
                care.warned = false;
                continue;
            }
            care.neglected += dt;
            if (care.neglected >= kCare.warning && !care.warned)
            {
                care.warned = true;
                park::post(messages, { .text_key = std::string(kAboutToBeConfiscated), .subject_name = animal.name });
            }
            if (care.neglected >= kCare.confiscation)
            {
                // shortcut: the animal goes at once; the game's helicopter lifting its crate away comes with the
                // helicopter that lowers new ones.
                park::post(messages, { .text_key = std::string(kConfiscated), .subject_name = animal.name });
                taken.push_back(e);
            }
        }
        for (const entt::entity e : taken)
        {
            for (auto [k, duty] : r.view<KeeperDuty>().each())
                if (duty.target == e)
                {
                    duty.job = KeeperJob::Idle;
                    duty.target = entt::null;
                }
            r.destroy(e);
        }
    }

} // namespace rct3::simulation
