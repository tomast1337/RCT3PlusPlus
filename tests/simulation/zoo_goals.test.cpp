// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/zoo_goals.hpp"

#include "core/types.hpp"
#include "simulation/objectives.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3;
using save::model::ConditionKind;

namespace
{
    save::model::Condition animal_goal(ConditionKind kind, i64 value, std::string species)
    {
        return { .kind = kind, .value = value, .animals = save::model::AnimalTarget { .species = std::move(species) } };
    }
} // namespace

TEST_CASE("every zoo goal can be judged")
{
    for (const ConditionKind kind : { ConditionKind::AnimalsOfSpecies, ConditionKind::AnimalSalesProfit, ConditionKind::AnimalsReleased, ConditionKind::EscapedAnimals, ConditionKind::ViewingGalleries, ConditionKind::GalleryVisitors, ConditionKind::GalleryIncome })
        CHECK(simulation::judged(kind));
}

TEST_CASE("a goal on a species' animals counts those of the species, whatever the case of its name")
{
    const std::array<simulation::GoalAnimal, 4> animals { { { "MadeUpBird" }, { "madeupbird", true }, { "MadeUpCat" }, { "MadeUpBird" } } };
    const simulation::ParkFigures f { .animals = animals };
    save::model::Condition birds = animal_goal(ConditionKind::AnimalsOfSpecies, 3, "MadeUpBird");
    CHECK(simulation::read_goal(birds, f).now == 3);
    CHECK(simulation::read_goal(birds, f).met);
    SUBCASE("bought animals left out, only those born in the park count")
    {
        birds.animals->bought_count = false;
        CHECK(simulation::read_goal(birds, f).now == 1);
        CHECK_FALSE(simulation::read_goal(birds, f).met);
    }
    SUBCASE("a goal whose number named no species counts nothing")
    {
        CHECK(simulation::read_goal(animal_goal(ConditionKind::AnimalsOfSpecies, 0, ""), f).now == 0);
    }
}

TEST_CASE("profit from selling a species' animals is its sales less its purchases over every month")
{
    // Sales are kept below nought, purchases above.
    const simulation::ParkFigures f {
        .this_month = { .species_spending = { { .species = "MadeUpBird", .spending = { .purchase_sale = -4'000 } }, { .species = "MadeUpCat", .spending = { .purchase_sale = -90'000 } } } },
        .months_before = { .species_spending = { { .species = "MadeUpBird", .spending = { .purchase_sale = -10'000 } } } },
    };
    CHECK(simulation::read_goal(animal_goal(ConditionKind::AnimalSalesProfit, 14'000, "MadeUpBird"), f).now == 14'000);
    CHECK(simulation::read_goal(animal_goal(ConditionKind::AnimalSalesProfit, 14'000, "MadeUpBird"), f).met);
    CHECK(simulation::read_goal(animal_goal(ConditionKind::AnimalSalesProfit, 14'000, "MadeUpDog"), f).now == 0);
}

TEST_CASE("animals set free and escaped animals caught are the counts the goal keeps; caught ones only once they got out")
{
    save::model::Condition released = animal_goal(ConditionKind::AnimalsReleased, 5, "MadeUpBird");
    released.animals->done = 5;
    CHECK(simulation::read_goal(released, { }).met);
    save::model::Condition caught { .kind = ConditionKind::EscapedAnimals, .value = 2, .animals = save::model::AnimalTarget { .done = 2 }, .challenge = 0 };
    CHECK(simulation::read_goal(caught, { }).now == 2);
    CHECK_FALSE(simulation::read_goal(caught, { }).met);
    caught.animals->started = true;
    CHECK(simulation::read_goal(caught, { }).met);
}

TEST_CASE("gallery goals read the galleries built, their visitors and last month's tickets")
{
    const simulation::ParkFigures f { .last_month = { .viewing_gallery_tickets = -2'500 }, .galleries = 4, .gallery_visitors = 30 };
    CHECK(simulation::read_goal({ .kind = ConditionKind::ViewingGalleries, .value = 4 }, f).met);
    CHECK(simulation::read_goal({ .kind = ConditionKind::GalleryIncome, .value = 2'000 }, f).now == 2'500);
    save::model::Condition visitors { .kind = ConditionKind::GalleryVisitors, .value = 50, .gallery = save::model::GalleryTarget { } };
    CHECK(simulation::read_goal(visitors, f).now == 30);
    visitors.gallery->visitors = 60; // As the file kept them, galleries since pulled down among them
    CHECK(simulation::read_goal(visitors, f).met);
}
