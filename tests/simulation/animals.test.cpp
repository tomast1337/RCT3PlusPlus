// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/animals.hpp"

#include "simulation/components.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <doctest/doctest.h>
#include <numbers>

using namespace rct3;
using namespace rct3::simulation;

namespace
{
    /// Flat ground, one unit to a tile: enclosure 0 the square from (0, 0) to (4, 4), enclosure 1 from (10, 0)
    /// to (14, 4).
    AnimalGround two_pens()
    {
        return {
            .height = [](glm::vec2) { return 0.0f; },
            .enclosure = [](glm::vec2 p) { return p.y >= 0 && p.y < 4 ? (p.x >= 0 && p.x < 4         ? 0
                                                                             : p.x >= 10 && p.x < 14 ? 1
                                                                                                     : -1)
                                                                      : -1; },
            .somewhere = [](std::int32_t which, random::RandomState& dice) -> std::optional<glm::vec2> {
                if (which < 0 || which > 1)
                    return std::nullopt;
                return glm::vec2 { static_cast<float>(which * 10) + static_cast<float>(random::between(dice, 0.0, 4.0)), static_cast<float>(random::between(dice, 0.0, 4.0)) };
            },
        };
    }

    void run(SimWorld& world, const AnimalGround& ground, float seconds, ParkMessages& messages)
    {
        for (float t = 0; t < seconds; t += 0.25f)
            step_animals(world, ground, { 0.25f }, messages);
    }

    void run(SimWorld& world, const AnimalGround& ground, float seconds)
    {
        ParkMessages messages;
        run(world, ground, seconds, messages);
    }
} // namespace

TEST_CASE("animals: an animal house comes with a paid keeper at the given spot")
{
    SimWorld world;
    const entt::entity keeper = add_animal_house(world, { .object = 7, .hut = 8, .enclosure = 1 }, { 11, 0, 1 });
    const auto& r = world.registry;
    CHECK(r.get<Staff>(keeper).role == StaffRole::Keeper);
    CHECK(r.get<Staff>(keeper).wage.value > 0);
    CHECK(r.get<Pose>(keeper).position == glm::vec3 { 11, 0, 1 });
    const entt::entity house = r.get<KeeperDuty>(keeper).house;
    CHECK(r.get<AnimalHouse>(house).object == 7);
}

TEST_CASE("animals: a keeper strolls about their own enclosure and nowhere else, never to the house")
{
    SimWorld world;
    const AnimalGround ground = two_pens();
    const entt::entity keeper = add_animal_house(world, { .enclosure = 1, .troughs = { 13, 0, 2 }, .low = { 10, 0 }, .high = { 12, 4 } }, { 12, 0, 2 });
    const glm::vec3 first = world.registry.get<Pose>(keeper).position;
    bool moved = false;
    for (int i = 0; i < 400; ++i)
    {
        run(world, ground, 1);
        const glm::vec3 at = world.registry.get<Pose>(keeper).position;
        REQUIRE(ground.enclosure({ at.x, at.z }) == 1);
        const KeeperDuty& duty = world.registry.get<KeeperDuty>(keeper);
        REQUIRE((duty.job == KeeperJob::Idle || duty.goal.x >= 12));
        moved = moved || at != first;
    }
    CHECK(moved);
}

namespace
{
    /// A house in enclosure 0 with its troughs at (3, 2), and an animal of `species` let out of its crate at
    /// (1, 1); returns the animal.
    entt::entity animal_at_home(SimWorld& world, const AnimalGround& ground, std::string species = "zebu")
    {
        (void)add_animal_house(world, { .enclosure = 0, .troughs = { 3, 0, 2 }, .low = { 3.5f, 0 }, .high = { 4, 4 } }, { 3, 0, 3 });
        const entt::entity e = add_animal(world, { .species = std::move(species), .enclosure = 0 }, { 1, 0, 1 });
        run(world, ground, kAnimalRates.crated.value + 0.5f);
        return e;
    }
} // namespace

TEST_CASE("animals: an animal stays in its crate a while, then walks out about its enclosure")
{
    SimWorld world;
    const AnimalGround ground = two_pens();
    const entt::entity e = add_animal(world, { .species = "zebu", .enclosure = 0 }, { 1, 0, 1 });
    run(world, ground, kAnimalRates.crated.value - 1);
    CHECK(world.registry.get<Animal>(e).job == AnimalJob::Crated);
    CHECK(world.registry.get<Pose>(e).position == glm::vec3 { 1, 0, 1 });
    bool moved = false;
    for (int i = 0; i < 120; ++i)
    {
        run(world, ground, 1);
        const glm::vec3 at = world.registry.get<Pose>(e).position;
        REQUIRE(ground.enclosure({ at.x, at.z }) == 0);
        moved = moved || at != glm::vec3 { 1, 0, 1 };
    }
    CHECK(moved);
}

TEST_CASE("animals: a hungry animal eats at its house's troughs, which empty as it does")
{
    SimWorld world;
    const AnimalGround ground = two_pens();
    const entt::entity e = animal_at_home(world, ground);
    auto& r = world.registry;
    r.get<Animal>(e).hunger = { 0.9f };
    run(world, ground, 30);
    CHECK(r.get<Animal>(e).hunger.value < 0.5f);
    const auto houses = r.view<const AnimalHouse>();
    CHECK(houses.get<const AnimalHouse>(houses.front()).food.value < 1);
}

TEST_CASE("animals: the keeper fills the troughs once they run low")
{
    SimWorld world;
    const AnimalGround ground = two_pens();
    (void)animal_at_home(world, ground);
    auto& r = world.registry;
    const auto houses = r.view<AnimalHouse>();
    AnimalHouse& house = houses.get<AnimalHouse>(houses.front());
    house.food = { 0.1f };
    house.water = { 0.2f };
    run(world, ground, 40);
    CHECK(house.food.value > 0.8f);
    CHECK(house.water.value > 0.8f);
    CHECK(house.since_fed.value < 40);
}

TEST_CASE("animals: an animal leaves dung now and then, which its keeper clears")
{
    SimWorld world;
    const AnimalGround ground = two_pens();
    (void)animal_at_home(world, ground);
    auto& r = world.registry;
    const auto dung = [&] { return r.view<const Dung>().size(); };
    std::size_t most = 0;
    for (int i = 0; i < static_cast<int>(kAnimalRates.dung.value) * 3; ++i)
    {
        run(world, ground, 1);
        most = std::max(most, dung());
    }
    CHECK(most >= 1);
    run(world, ground, 60);
    CHECK(dung() <= 1); // Cleared, bar what was just left
}

TEST_CASE("animals: with empty troughs and no keeper to fill them, a starving animal sickens")
{
    SimWorld world;
    const AnimalGround ground = two_pens();
    const entt::entity e = animal_at_home(world, ground);
    auto& r = world.registry;
    r.destroy(r.view<KeeperDuty>().front());
    const auto houses = r.view<AnimalHouse>();
    houses.get<AnimalHouse>(houses.front()).food = { };
    houses.get<AnimalHouse>(houses.front()).water = { };
    run(world, ground, kAnimalRates.thirsty.value + kAnimalRates.sickening.value / 2);
    CHECK(r.get<Animal>(e).health.value < 0.75f);
}

TEST_CASE("animals: an animal alone grows lonely; one with its own kind about does not")
{
    SimWorld world;
    const AnimalGround ground = two_pens();
    const entt::entity alone = animal_at_home(world, ground, "zebu");
    const entt::entity a = add_animal(world, { .species = "yak", .enclosure = 1 }, { 11, 0, 1 });
    const entt::entity b = add_animal(world, { .species = "yak", .enclosure = 1 }, { 12, 0, 1 });
    run(world, ground, 120);
    auto& r = world.registry;
    CHECK(r.get<Animal>(alone).loneliness.value > 0.2f);
    CHECK(r.get<Animal>(a).loneliness.value == 0);
    CHECK(r.get<Animal>(b).loneliness.value == 0);
}

TEST_CASE("animals: an animal let out of its crate outside every enclosure escapes, and the park says so")
{
    SimWorld world;
    const AnimalGround ground = two_pens();
    ParkMessages messages;
    const entt::entity e = add_animal(world, { .species = "zebu", .name = "Zebu" }, { 7, 0, 2 });
    run(world, ground, kAnimalRates.crated.value + 1, messages);
    const Animal& animal = world.registry.get<Animal>(e);
    CHECK(animal.escaped);
    REQUIRE(messages.list.size() == 1);
    CHECK(messages.list.front().text_key == kAnimalEscaped);
    CHECK(messages.list.front().subject_name == "Zebu");
    run(world, ground, 20, messages);
    CHECK(messages.list.size() == 1); // Said once
}

TEST_CASE("animals: an animal whose enclosure is taken from under it escapes")
{
    SimWorld world;
    AnimalGround ground = two_pens();
    const entt::entity e = add_animal(world, { .species = "zebu", .enclosure = 0 }, { 1, 0, 1 });
    run(world, ground, kAnimalRates.crated.value + 1);
    CHECK_FALSE(world.registry.get<Animal>(e).escaped);
    ground.enclosure = [](glm::vec2) { return -1; };
    run(world, ground, 1);
    CHECK(world.registry.get<Animal>(e).escaped);
}

TEST_CASE("animals: a keeper crates an escaped animal and takes it back to their enclosure, where it comes out again")
{
    SimWorld world;
    const AnimalGround ground = two_pens();
    (void)add_animal_house(world, { .enclosure = 1, .troughs = { 13, 0, 2 }, .low = { 13.5f, 0 }, .high = { 14, 4 } }, { 12, 0, 2 });
    const entt::entity e = add_animal(world, { .species = "zebu" }, { 7, 0, 2 });
    auto& r = world.registry;
    run(world, ground, kAnimalRates.crated.value + 1);
    REQUIRE(r.get<Animal>(e).escaped);
    bool caught = false;
    for (int i = 0; i < 240 && !caught; ++i)
    {
        run(world, ground, 1);
        const Animal& animal = r.get<Animal>(e);
        caught = !animal.escaped && animal.enclosure == 1;
    }
    REQUIRE(caught);
    const glm::vec3 at = r.get<Pose>(e).position;
    CHECK(ground.enclosure({ at.x, at.z }) == 1);
    CHECK(r.get<Animal>(e).job == AnimalJob::Crated);
    run(world, ground, kAnimalRates.crated.value + 1);
    CHECK(r.get<Animal>(e).job != AnimalJob::Crated);
    CHECK_FALSE(r.get<Animal>(e).escaped);
}

TEST_CASE("animals: a park's saved zoo animals come in where they stood, those in crates still crated")
{
    SimWorld world;
    const AnimalGround ground = two_pens();
    save::ZooAnimal out;
    out.species = "zebu";
    out.first_names = "Zia";
    out.is_adult = true;
    out.health = 0.5f;
    out.world = { 0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 2, 0, 3, 1 }; // Facing along x, at (2, 0, 3)
    save::ZooAnimal crated = out;
    crated.in_crate = true;
    crated.world[12] = 11;
    const std::array saved { out, crated };
    import_animals(world, saved, ground.enclosure);
    auto& r = world.registry;
    const auto animals = r.view<const Animal, const Pose>();
    REQUIRE(std::ranges::distance(animals) == 2);
    for (const entt::entity e : animals)
    {
        const Animal& a = animals.get<const Animal>(e);
        const Pose& pose = animals.get<const Pose>(e);
        CHECK(a.name == "Zia");
        CHECK(a.health.value == 0.5f);
        if (pose.position.x == 2)
        {
            CHECK(a.job == AnimalJob::Idle);
            CHECK(a.enclosure == 0);
            CHECK(pose.heading.value == doctest::Approx(std::numbers::pi / 2));
        }
        else
        {
            CHECK(a.job == AnimalJob::Crated);
            CHECK(a.enclosure == 1);
        }
    }
}
