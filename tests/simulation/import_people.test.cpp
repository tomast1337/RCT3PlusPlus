// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/import_people.hpp"

#include "simulation/components.hpp"

#include <doctest/doctest.h>

using namespace rct3;

TEST_CASE("guests in the park come over with their needs turned the other way up")
{
    save::PeopleModel people;
    save::Guest g;
    g.person.type = save::PersonType::Child;
    g.person.sub_type = std::to_underlying(save::Sex::Female);
    g.person.name = "Test Guest";
    g.person.position = { 10, 2, 30 };
    g.person.heading = 1.25f;
    g.needs.food = 0.75f;  // Fairly full
    g.needs.drink = 0.25f; // Thirsty
    g.needs.toilet = 0.5f;
    g.mood_now.happiness = 0.9f;
    g.cash.current = 1500;
    g.cash.bank = 9000;
    g.cash.spent_rides = 400;
    people.guests.push_back(g);
    save::Guest outside = g;
    outside.in_park = false;
    people.guests.push_back(outside);

    simulation::SimWorld world;
    const auto made = simulation::import_guests(world, people);
    REQUIRE(made.size() == 2);
    CHECK(made[1] == entt::null);
    const auto view = world.registry.view<simulation::Visitor, simulation::Pose, simulation::Needs, simulation::Mood, simulation::Wallet>();
    REQUIRE(view.size_hint() == 1);
    const auto e = view.front();
    const auto& v = world.registry.get<simulation::Visitor>(e);
    CHECK(v.age == simulation::AgeGroup::Child);
    CHECK(v.female);
    CHECK(v.name == "Test Guest");
    CHECK(made[0] == e);
    CHECK(world.registry.get<simulation::Pose>(e).position == glm::vec3 { 10, 2, 30 });
    CHECK(world.registry.get<simulation::Pose>(e).heading.value == -1.25f);
    const auto& n = world.registry.get<simulation::Needs>(e);
    CHECK(n.hunger.value == doctest::Approx(0.25f));
    CHECK(n.thirst.value == doctest::Approx(0.75f));
    CHECK(n.bladder.value == doctest::Approx(0.5f));
    CHECK(world.registry.get<simulation::Mood>(e).happiness.value == doctest::Approx(0.9f));
    CHECK(world.registry.get<simulation::Wallet>(e).cash.value == 1500);
    CHECK(world.registry.get<simulation::Wallet>(e).bank.value == 9000);
    CHECK(world.registry.get<simulation::Wallet>(e).spent_rides.value == 400);
}
