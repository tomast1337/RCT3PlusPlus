// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/animal_care.hpp"

#include "core/types.hpp"
#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/park/messages.hpp"
#include "simulation/staff/care.hpp"

#include <algorithm>
#include <array>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3;
using namespace rct3::simulation;
using namespace rct3::simulation::park;

namespace
{
    /// Flat ground, one unit to a tile: enclosure 0 the square from (0, 0) to (10, 10), 100 tiles with no plants.
    AnimalGround pen()
    {
        return {
            .height = [](glm::vec2) { return 0.0f; },
            .enclosure = [](glm::vec2 p) -> std::optional<i32> {
                if (p.x >= 0 && p.x < 10 && p.y >= 0 && p.y < 10)
                    return 0;
                return std::nullopt;
            },
            .somewhere = [](std::optional<i32> which, random::RandomState& dice) -> std::optional<glm::vec2> {
                if (which != 0)
                    return std::nullopt;
                return glm::vec2 { static_cast<f32>(random::between(dice, 0.0, 10.0)), static_cast<f32>(random::between(dice, 0.0, 10.0)) };
            },
        };
    }

    constexpr std::array<EnclosureSite, 1> kRoomy { EnclosureSite { .tiles = 100, .plants = 20 } };

    void run(SimWorld& world, const AnimalGround& ground, std::span<const EnclosureSite> sites, f32 seconds, ParkMessages& messages)
    {
        for (f32 t = 0; t < seconds; t += 0.25f)
        {
            step_animals(world, ground, { 0.25f }, messages);
            step_animal_care(world, sites, { 0.25f }, messages);
            ++world.tick;
        }
    }

    void run(SimWorld& world, const AnimalGround& ground, std::span<const EnclosureSite> sites, f32 seconds)
    {
        ParkMessages messages;
        run(world, ground, sites, seconds, messages);
    }

    /// A house of kind `kind` in enclosure 0 with its troughs at (5, 5) and its keeper's hut at (9, 9). Returns its keeper.
    entt::entity house_of(SimWorld& world, std::string kind)
    {
        const entt::entity keeper = add_animal_house(world, { .enclosure = std::optional { 0 }, .troughs = { 5, 0, 5 }, .low = { 4, 4 }, .high = { 6, 6 } }, { 5, 0, 6 });
        world.registry.emplace<HouseCare>(world.registry.get<KeeperDuty>(keeper).house, HouseCare { .kind = std::move(kind), .hut = glm::vec3 { 9, 0, 9 } });
        return keeper;
    }

    /// An animal of `species` in enclosure 0 at `at`, eating at houses of kind `house`, wanting `space` tiles, out of
    /// its crate.
    entt::entity animal(SimWorld& world, std::string species, std::string house, i32 space, i32 sociability, glm::vec3 at)
    {
        const entt::entity e = add_animal(world, { .species = std::move(species), .enclosure = std::optional { 0 } }, at);
        world.registry.emplace<AnimalCare>(e, AnimalCare { .house = std::move(house), .space = space, .sociability = sociability });
        (void)open_crate(world, e);
        return e;
    }

    bool found(const SimWorld& world, Finding finding, entt::entity about)
    {
        const auto* findings = world.registry.ctx().find<Findings>();
        return findings != nullptr && std::ranges::contains(findings->found, Findings::Found { finding, about });
    }

    void inspect(SimWorld& world)
    {
        world.registry.emplace<Staff>(world.registry.create(), Staff { .role = StaffRole::Inspector });
    }
} // namespace

TEST_CASE("animal care: an animal with only the wrong kind of house starves, and the inspector says it needs its own")
{
    SimWorld world;
    const AnimalGround ground = pen();
    inspect(world);
    (void)house_of(world, "Burrow");
    const entt::entity e = animal(world, "zebu", "Stable", 10, 0, { 2, 0, 2 });
    const entt::entity fed = animal(world, "wombat", "burrow", 10, 0, { 3, 0, 2 });
    run(world, ground, kRoomy, kAnimalRates.hungry.value + kAnimalRates.sickening.value / 2);
    auto& r = world.registry;
    CHECK(r.get<Animal>(e).hunger.value >= 1);
    CHECK(r.get<Animal>(e).health.value < 0.75f);
    CHECK(r.get<Animal>(fed).hunger.value < 1);
    CHECK(found(world, Finding::AnimalNeedsHouse, e));
    CHECK_FALSE(found(world, Finding::AnimalNeedsHouse, fed));
}

TEST_CASE("animal care: crowding an enclosure lowers its animals' habitat, and the inspector calls it overcrowded")
{
    SimWorld world;
    const AnimalGround ground = pen();
    inspect(world);
    (void)house_of(world, "");
    const entt::entity first = animal(world, "zebu", "", 60, 8, { 2, 0, 2 });
    run(world, ground, kRoomy, 1);
    const f32 alone = world.registry.get<AnimalCare>(first).habitat.value;
    CHECK(alone > 0.9f);
    for (i32 i = 0; i < 6; ++i)
        (void)animal(world, "zebu", "", 60, 8, { 3, 0, static_cast<f32>(i) + 1 });
    run(world, ground, kRoomy, 1);
    const AnimalCare& care = world.registry.get<AnimalCare>(first);
    CHECK(care.habitat.value < alone - 0.2f);
    CHECK(care.most_health.value < 1);
    // Told once for the enclosure, about whichever of its animals.
    const auto& told = world.registry.ctx().get<Findings>().found;
    CHECK(std::ranges::count(told, Finding::EnclosureOvercrowded, &Findings::Found::finding) == 1);
}

TEST_CASE("animal care: an enclosure smaller than one animal of its species wants is too small")
{
    SimWorld world;
    const AnimalGround ground = pen();
    inspect(world);
    const entt::entity e = animal(world, "zebu", "", 150, 8, { 2, 0, 2 });
    run(world, ground, kRoomy, 1);
    CHECK(found(world, Finding::EnclosureTooSmall, e));
}

TEST_CASE("animal care: a social animal alone loses health it does not mend, where one with company keeps well")
{
    SimWorld world;
    const AnimalGround ground = pen();
    inspect(world);
    (void)house_of(world, "");
    const entt::entity lone = animal(world, "zebu", "", 10, 16, { 2, 0, 2 });
    auto& r = world.registry;
    r.get<Animal>(lone).health = { 1 };
    run(world, ground, kRoomy, kAnimalRates.lonely.value * 2);
    CHECK(r.get<Animal>(lone).loneliness.value >= 1);
    CHECK(r.get<Animal>(lone).health.value < 0.8f);
    CHECK(found(world, Finding::AnimalLonely, lone));

    SimWorld herd_world;
    (void)house_of(herd_world, "");
    std::vector<entt::entity> herd;
    for (i32 i = 0; i < 5; ++i)
        herd.push_back(animal(herd_world, "zebu", "", 10, 16, { 2, 0, static_cast<f32>(i) + 1 }));
    run(herd_world, ground, kRoomy, kAnimalRates.lonely.value * 2);
    CHECK(herd_world.registry.get<Animal>(herd[0]).loneliness.value == 0);
    CHECK(herd_world.registry.get<Animal>(herd[0]).health.value > 0.9f);
}

TEST_CASE("animal care: an animal that wants little company is annoyed by a crowd of other animals")
{
    SimWorld world;
    const AnimalGround ground = pen();
    inspect(world);
    const entt::entity loner = animal(world, "panther", "", 10, 2, { 2, 0, 2 });
    for (i32 i = 0; i < 5; ++i)
        (void)animal(world, "zebu", "", 10, 16, { 4, 0, static_cast<f32>(i) + 1 });
    run(world, ground, kRoomy, 1);
    CHECK(world.registry.get<AnimalCare>(loner).annoyed);
    CHECK(found(world, Finding::AnimalAntiSocial, loner));
}

TEST_CASE("animal care: a neglected animal brings a warning, then is confiscated; one that mends in time is not")
{
    SimWorld world;
    const AnimalGround ground = pen();
    const entt::entity e = animal(world, "zebu", "", 10, 0, { 2, 0, 2 });
    auto& r = world.registry;
    r.get<Animal>(e).name = "Zed";
    r.get<Animal>(e).health = { 0.05f };
    r.get<Animal>(e).hunger = { 1 };
    ParkMessages messages;
    const auto posted = [&](std::string_view key) { return std::ranges::count_if(messages.list, [&](const ParkMessage& m) { return m.text_key == key && m.subject_name == "Zed"; }); };
    run(world, ground, kRoomy, kAnimalCareRates.warning.value + 1, messages);
    CHECK(posted(kAboutToBeConfiscated) == 1);
    CHECK(posted(kConfiscated) == 0);
    CHECK(r.valid(e));
    run(world, ground, kRoomy, kAnimalCareRates.confiscation.value - kAnimalCareRates.warning.value, messages);
    CHECK(posted(kAboutToBeConfiscated) == 1);
    CHECK(posted(kConfiscated) == 1);
    CHECK_FALSE(r.valid(e));

    // Fed again by a keeper after the warning, it mends and stays.
    SimWorld kept;
    (void)house_of(kept, "");
    const entt::entity k = animal(kept, "zebu", "", 10, 0, { 2, 0, 2 });
    kept.registry.get<Animal>(k).health = { 0.05f };
    kept.registry.get<Animal>(k).hunger = { 1 };
    ParkMessages told;
    run(kept, ground, kRoomy, kAnimalCareRates.confiscation.value * 2, told);
    CHECK(kept.registry.valid(k));
    CHECK(kept.registry.get<Animal>(k).health.value > kAnimalCareRates.neglect.value);
}

TEST_CASE("animal care: an animal finding every trough of its house taken goes without, and the inspector says so")
{
    SimWorld world;
    const AnimalGround ground = pen();
    inspect(world);
    (void)house_of(world, "");
    std::vector<entt::entity> herd;
    for (i32 i = 0; i < kAnimalCareRates.places + 1; ++i)
        herd.push_back(animal(world, "zebu", "", 1, 20, { 5, 0, 5 }));
    auto& r = world.registry;
    for (const entt::entity e : herd)
    {
        Animal& a = r.get<Animal>(e);
        a.job = AnimalJob::Feeding;
        a.goal = { 5, 0, 5 };
        a.busy = { 1000 };
    }
    const entt::entity late = herd.back();
    r.get<Animal>(late).job = AnimalJob::Idle;
    r.get<Animal>(late).hunger = { 0.9f };
    ParkMessages messages;
    for (i32 i = 0; i < kAnimalCareRates.tries && !found(world, Finding::HouseFull, late); ++i)
    {
        r.get<Animal>(late).job = AnimalJob::Idle;
        r.get<Animal>(late).busy = { };
        step_animals(world, ground, { 0.25f }, messages);
    }
    CHECK(r.get<Animal>(late).job != AnimalJob::Feeding);
    CHECK(found(world, Finding::HouseFull, late));
}

TEST_CASE("animal care: an animal with a cliff between it and its house cannot reach it")
{
    SimWorld world;
    AnimalGround ground = pen();
    ground.height = [](glm::vec2 p) { return p.x >= 4 ? 10.0f : 0.0f; };
    inspect(world);
    (void)house_of(world, "");
    const entt::entity e = animal(world, "zebu", "", 1, 20, { 1, 0, 5 });
    auto& r = world.registry;
    r.get<Animal>(e).hunger = { 0.9f };
    ParkMessages messages;
    for (i32 i = 0; i < kAnimalCareRates.tries; ++i)
    {
        r.get<Animal>(e).job = AnimalJob::Idle;
        r.get<Animal>(e).busy = { };
        r.get<Pose>(e).position = { 1, 0, 5 };
        step_animals(world, ground, { 0.25f }, messages);
        CHECK(r.get<Animal>(e).job != AnimalJob::Feeding);
    }
    CHECK(found(world, Finding::CannotReachHouse, e));
}

TEST_CASE("animal care: a keeper changing from filling troughs to clearing dung fetches the shovel from the hut first")
{
    SimWorld world;
    const AnimalGround ground = pen();
    const entt::entity keeper = house_of(world, "");
    auto& r = world.registry;
    const entt::entity d = r.create();
    r.emplace<Dung>(d, Dung { std::optional { 0 } });
    r.emplace<Pose>(d, Pose { .position = { 2, 0, 2 }, .was = { 2, 0, 2 } });
    r.get<KeeperDuty>(keeper).busy = { };
    bool swapped = false;
    ParkMessages messages;
    for (i32 i = 0; i < 400 && r.valid(d); ++i)
    {
        step_animals(world, ground, { 0.25f }, messages);
        const KeeperDuty& duty = r.get<KeeperDuty>(keeper);
        if (duty.job == KeeperJob::Swapping)
        {
            swapped = true;
            CHECK(duty.goal == glm::vec3 { 9, 0, 9 });
        }
    }
    CHECK(swapped);
    CHECK_FALSE(r.valid(d));
    CHECK(r.get<KeeperTool>(keeper).holding == KeeperJob::Clearing);
}

TEST_CASE("animal care: a keeper with no hut to reach clears with the tool in hand, and the inspector says so")
{
    SimWorld world;
    const AnimalGround ground = pen();
    inspect(world);
    const entt::entity keeper = house_of(world, "");
    auto& r = world.registry;
    r.get<HouseCare>(r.get<KeeperDuty>(keeper).house).hut.reset();
    const entt::entity d = r.create();
    r.emplace<Dung>(d, Dung { std::optional { 0 } });
    r.emplace<Pose>(d, Pose { .position = { 2, 0, 2 }, .was = { 2, 0, 2 } });
    r.get<KeeperDuty>(keeper).busy = { };
    ParkMessages messages;
    for (i32 i = 0; i < 400 && r.valid(d); ++i)
    {
        step_animals(world, ground, { 0.25f }, messages);
        REQUIRE(r.get<KeeperDuty>(keeper).job != KeeperJob::Swapping);
    }
    CHECK_FALSE(r.valid(d));
    CHECK(found(world, Finding::KeeperCantReachHut, keeper));
}

TEST_CASE("animal care: an untrained keeper takes longer at a job than a trained one")
{
    CHECK(at_training({ 10 }, staff::kMostTraining).value == doctest::Approx(10));
    CHECK(at_training({ 10 }, 0).value > 10);
    CHECK(at_training({ 10 }, 1).value > at_training({ 10 }, 2).value);
}

TEST_CASE("animal care: animals of a species differ a little in appetite and sociability, near their species'")
{
    random::RandomState dice = random::seeded(random::Seed { 7 });
    std::vector<i32> appetites;
    for (i32 i = 0; i < 40; ++i)
    {
        const AnimalCare care = care_for({ .space = 60, .appetite = 8, .sociability = 14 }, "Stable", dice);
        CHECK(care.space == 60);
        CHECK(care.house == "Stable");
        CHECK(std::abs(care.appetite - 8) <= kAnimalCareRates.spread);
        CHECK(std::abs(care.sociability - 14) <= kAnimalCareRates.spread);
        appetites.push_back(care.appetite);
    }
    CHECK(std::ranges::min(appetites) != std::ranges::max(appetites));
    // A hungrier animal is hungry sooner.
    SimWorld world;
    const entt::entity big = add_animal(world, { }, { });
    const entt::entity small = add_animal(world, { }, { });
    world.registry.emplace<AnimalCare>(big, AnimalCare { .appetite = 15 });
    world.registry.emplace<AnimalCare>(small, AnimalCare { .appetite = 5 });
    CHECK(hungry_after(world.registry, big) < hungry_after(world.registry, small));
}
