// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/animal_release.hpp"

#include "core/types.hpp"
#include "simulation/objectives.hpp"
#include "simulation/tuning.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using save::model::ChallengeState;
using save::model::ConditionKind;

namespace
{
    save::model::Condition released_goal(std::string species, std::optional<i32> challenge = std::nullopt)
    {
        return { .kind = ConditionKind::AnimalsReleased, .value = 2, .animals = save::model::AnimalTarget { .species = std::move(species) }, .challenge = challenge };
    }
} // namespace

TEST_CASE("only a grown animal at least 95% healthy may be set free")
{
    simulation::Animal a { .species = "MadeUpCat", .adult = true, .health = units::Share { 0.95f } };
    CHECK(simulation::may_release(a));
    a.health = units::Share { 0.94f };
    CHECK_FALSE(simulation::may_release(a));
    a = { .species = "MadeUpCat", .adult = false, .health = units::Share { 1 } };
    CHECK_FALSE(simulation::may_release(a));
}

TEST_CASE("a release counts on the scenario's goals and the running challenges' of its species")
{
    save::model::ObjectiveSet goals {
        .conditions = { released_goal("MadeUpCat"), released_goal("madeupcat", 1), released_goal("MadeUpCat", 2), released_goal("MadeUpBird"), { .kind = ConditionKind::AnimalsOfSpecies, .value = 2, .animals = save::model::AnimalTarget { .species = "MadeUpCat" } } },
        .challenges = { { .challenge_id = 1, .state = ChallengeState::Running }, { .challenge_id = 2, .state = ChallengeState::Waiting } },
    };
    simulation::count_release(goals, "MadeUpCat");
    simulation::count_release(goals, "MadeUpCat");
    CHECK(goals.conditions[0].animals->done == 2);
    CHECK(simulation::read_goal(goals.conditions[0], { }).met);
    CHECK(goals.conditions[1].animals->done == 2); // Its species named in another case
    CHECK(goals.conditions[2].animals->done == 0); // A challenge not taken on
    CHECK(goals.conditions[3].animals->done == 0);
    CHECK(goals.conditions[4].animals->done == 0);
}

TEST_CASE("an animal set free leaves the park and draws parties; one refused stays")
{
    simulation::SimWorld world;
    save::model::ObjectiveSet goals { .conditions = { released_goal("MadeUpCat") } };
    const entt::entity young = simulation::add_animal(world, { .species = "MadeUpCat", .adult = false }, { });
    CHECK_FALSE(simulation::release_animal(world, young, 0.5f, goals, 100));
    CHECK(world.registry.valid(young));
    CHECK(goals.publicity_until == 0);

    const entt::entity grown = simulation::add_animal(world, { .species = "MadeUpCat" }, { });
    CHECK(simulation::release_animal(world, grown, 0.5f, goals, 100));
    CHECK_FALSE(world.registry.valid(grown));
    CHECK(goals.conditions[0].animals->done == 1);
    CHECK(goals.publicity_until == doctest::Approx(100 + 0.5f * simulation::kTuning.prizes.publicity.value));
}
