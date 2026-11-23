// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/galleries.hpp"

#include "core/types.hpp"
#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/sim_world.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation;

namespace
{
    /// A plain enclosure: 40 tiles, all level, nothing planted, no water, with three healthy animals that want 30
    /// tiles between them.
    EnclosureLook plain()
    {
        return { .site = { .tiles = 40, .plants = 0, .water = 0, .level = 40 }, .animals = 3, .health = { 1 }, .room_wanted = 30 };
    }
} // namespace

TEST_CASE("an enclosure rates higher as it grows, greens, holds water and its animals are well")
{
    const EnclosureLook base = plain();
    const f32 rating = enclosure_rating(base).value;
    CHECK(rating > 0);
    CHECK(rating < 1);

    EnclosureLook bigger = base;
    bigger.site.tiles = bigger.site.level = 100;
    CHECK(enclosure_rating(bigger).value > rating);

    EnclosureLook greener = base;
    greener.site.plants = 30;
    CHECK(enclosure_rating(greener).value > rating);

    EnclosureLook watery = base;
    watery.site.water = 6;
    CHECK(enclosure_rating(watery).value > rating);

    EnclosureLook livelier = base;
    livelier.animals = 4;
    CHECK(enclosure_rating(livelier).value > rating);

    EnclosureLook unwell = base;
    unwell.health = { 0.3f };
    CHECK(enclosure_rating(unwell).value < rating);

    EnclosureLook crowded = base;
    crowded.room_wanted = 200;
    CHECK(enclosure_rating(crowded).value < rating);
}

TEST_CASE("an enclosure with no animals has nothing to see")
{
    EnclosureLook empty = plain();
    empty.animals = 0;
    CHECK(enclosure_rating(empty).value == 0);
}

TEST_CASE("a gallery takes the rating of the enclosure it looks into from the animals living there")
{
    SimWorld world;
    auto& r = world.registry;
    const std::array<EnclosureSite, 2> sites { { { .tiles = 100, .plants = 40, .water = 10, .level = 100 }, { .tiles = 100, .plants = 40, .water = 10, .level = 100 } } };
    for (const f32 health : { 1.0f, 0.9f })
    {
        const entt::entity a = r.create();
        r.emplace<Animal>(a, Animal { .enclosure = 0, .job = AnimalJob::Idle, .health = { health } });
    }
    const entt::entity sick = r.create();
    r.emplace<Animal>(sick, Animal { .enclosure = 1, .job = AnimalJob::Idle, .health = { 0.2f } });
    const entt::entity crated = r.create();
    r.emplace<Animal>(crated, Animal { .enclosure = 1, .job = AnimalJob::Crated });
    const entt::entity fine = r.create();
    r.emplace<Gallery>(fine, Gallery { .enclosure = 0 });
    const entt::entity poor = r.create();
    r.emplace<Gallery>(poor, Gallery { .enclosure = 1 });
    const entt::entity lost = r.create();
    r.emplace<Gallery>(lost, Gallery { .enclosure = 5 });

    rate_galleries(world, sites);
    CHECK(r.get<Gallery>(fine).excitement.value > r.get<Gallery>(poor).excitement.value);
    CHECK(r.get<Gallery>(fine).animals_health.value == doctest::Approx(0.95f));
    CHECK(r.get<Gallery>(poor).animals_health.value == doctest::Approx(0.2f));
    CHECK(r.get<Gallery>(lost).excitement.value == 0);
}
