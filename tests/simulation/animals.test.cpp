// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/animals.hpp"

#include "simulation/components.hpp"

#include <cmath>
#include <doctest/doctest.h>

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

    void run(SimWorld& world, const AnimalGround& ground, float seconds)
    {
        for (float t = 0; t < seconds; t += 0.25f)
            step_animals(world, ground, { 0.25f });
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
    const entt::entity keeper = add_animal_house(world, { .enclosure = 1, .low = { 10, 0 }, .high = { 12, 4 } }, { 12, 0, 2 });
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
