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
    people.guests.push_back(g);
    save::Guest outside = g;
    outside.in_park = false;
    people.guests.push_back(outside);

    simulation::SimWorld world;
    simulation::import_guests(world, people);
    const auto view = world.registry.view<simulation::Visitor, simulation::Pose, simulation::Needs, simulation::Mood, simulation::Wallet>();
    REQUIRE(view.size_hint() == 1);
    const auto e = view.front();
    const auto& v = world.registry.get<simulation::Visitor>(e);
    CHECK(v.age == simulation::AgeGroup::Child);
    CHECK(v.female);
    CHECK(v.name == "Test Guest");
    CHECK(world.registry.get<simulation::Pose>(e).position == glm::vec3 { 10, 2, 30 });
    const auto& n = world.registry.get<simulation::Needs>(e);
    CHECK(n.hunger == doctest::Approx(0.25f));
    CHECK(n.thirst == doctest::Approx(0.75f));
    CHECK(n.bladder == doctest::Approx(0.5f));
    CHECK(world.registry.get<simulation::Mood>(e).happiness == doctest::Approx(0.9f));
    CHECK(world.registry.get<simulation::Wallet>(e).cash == 1500);
}
