// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/wild_birds.hpp"

#include "simulation/guests.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation;

namespace
{
    constexpr units::Seconds kStep { 0.25f };

    /// Flat ground over a 200 square park, with a pond from (80, 80) to (120, 120).
    BirdGround pond_park()
    {
        const auto in_pond = [](glm::vec2 p) { return p.x >= 80 && p.x <= 120 && p.y >= 80 && p.y <= 120; };
        return {
            .height = [](glm::vec2) { return 0.0f; },
            .water = [=](glm::vec2 p) -> std::optional<float> { return in_pond(p) ? std::optional { -0.5f } : std::nullopt; },
            .water_spot = [](random::RandomState& dice) -> std::optional<glm::vec2> { return glm::vec2 { static_cast<float>(random::between(dice, 82.0, 118.0)), static_cast<float>(random::between(dice, 82.0, 118.0)) }; },
            .low = { 0, 0 },
            .high = { 200, 200 },
        };
    }

    void run(SimWorld& world, const BirdGround& ground, float seconds)
    {
        for (float t = 0; t < seconds; t += kStep.value)
            step_wild_birds(world, ground, kStep);
    }

    bool all_in(SimWorld& world, BirdJob job)
    {
        const auto view = world.registry.view<const WildBird>();
        return std::ranges::all_of(view, [&](entt::entity e) { return view.get<const WildBird>(e).job == job; });
    }
} // namespace

TEST_CASE("a flock of ducks comes down on the pond and stays on it")
{
    SimWorld world;
    const BirdGround ground = pond_park();
    world.registry.ctx().emplace<WildBirds>(WildBirds { .dice = random::seeded({ 3 }), .automatic = false });
    REQUIRE(add_ducks(world, ground, 12) == 12);
    run(world, ground, 60);
    CHECK(world.registry.view<WildBird>().size() == 12);
    CHECK(all_in(world, BirdJob::Resting));
    for (int i = 0; i < 40; ++i)
    {
        run(world, ground, 1);
        // Whenever it is down, a duck is over the pond; in the air on the way off it need not be.
        for (const auto [e, bird, pose] : world.registry.view<const WildBird, const Pose>().each())
            if (bird.job == BirdJob::Resting)
                CHECK(ground.water({ pose.position.x, pose.position.z }).has_value());
    }
}

TEST_CASE("ducks on the water keep together")
{
    SimWorld world;
    const BirdGround ground = pond_park();
    world.registry.ctx().emplace<WildBirds>(WildBirds { .dice = random::seeded({ 4 }), .automatic = false });
    add_ducks(world, ground, 10);
    run(world, ground, 60);
    glm::vec2 sum { 0.0f };
    for (const auto [e, pose] : world.registry.view<const Pose>().each())
        sum += glm::vec2 { pose.position.x, pose.position.z };
    const glm::vec2 middle = sum / 10.0f;
    for (const auto [e, pose] : world.registry.view<const Pose>().each())
        CHECK(glm::distance(middle, { pose.position.x, pose.position.z }) < 12.0f);
}

TEST_CASE("a guest walking up makes the flock take off")
{
    SimWorld world;
    const BirdGround ground = pond_park();
    world.registry.ctx().emplace<WildBirds>(WildBirds { .dice = random::seeded({ 5 }), .automatic = false });
    add_ducks(world, ground, 8);
    run(world, ground, 40);
    REQUIRE(all_in(world, BirdJob::Resting));
    glm::vec2 where { 0.0f };
    for (const auto [e, pose] : world.registry.view<const Pose>().each())
        where = { pose.position.x, pose.position.z };
    const entt::entity guest = world.registry.create();
    world.registry.emplace<Visitor>(guest);
    world.registry.emplace<Pose>(guest, Pose { .position = { where.x + 2, 0, where.y } });
    run(world, ground, 3);
    CHECK(std::ranges::none_of(world.registry.view<const WildBird>(), [&](entt::entity e) { return world.registry.get<const WildBird>(e).job == BirdJob::Resting; }));
}

TEST_CASE("a flock that is done leaves the park")
{
    SimWorld world;
    BirdGround ground = pond_park();
    ground.water_spot = nullptr;
    world.registry.ctx().emplace<WildBirds>(WildBirds { .dice = random::seeded({ 6 }), .automatic = false });
    add_ducks(world, pond_park(), 6);
    run(world, pond_park(), 30);
    run(world, ground, 400);
    CHECK(world.registry.view<WildBird>().empty());
}

TEST_CASE("no ducks come to a park without water")
{
    SimWorld world;
    BirdGround ground = pond_park();
    ground.water_spot = nullptr;
    CHECK(add_ducks(world, ground, 5) == 0);
}

TEST_CASE("a gull comes down at litter and then goes")
{
    SimWorld world;
    const BirdGround ground = pond_park();
    world.registry.ctx().emplace<WildBirds>(WildBirds { .dice = random::seeded({ 7 }), .automatic = false });
    add_gulls(world, ground, 3);
    CHECK(world.registry.view<WildBird>().size() == 3);
    run(world, ground, 300);
    CHECK(world.registry.view<WildBird>().empty());
    add_gulls(world, ground, 2);
    CHECK(remove_wild_birds(world) == 2);
}
