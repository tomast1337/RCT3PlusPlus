// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/debug_tools.hpp"

#include "core/types.hpp"
#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/parties.hpp"
#include "simulation/rides.hpp"
#include "simulation/sandbox.hpp"

#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <ranges>

using namespace rct3;
using namespace rct3::simulation;

namespace
{
    usize guests_in(const entt::registry& r)
    {
        return static_cast<usize>(std::ranges::distance(r.view<const Visitor>(entt::exclude<Left>)));
    }
} // namespace

TEST_CASE("debug tools: finishing research invents everything left at once")
{
    Research research { .spent = units::Money { 500 }, .current = 2, .focus = 2, .unfocused = 1, .set_aside = { { .id = 1, .spent = units::Money { 40 } } } };
    std::vector<ResearchItem> items { { .id = 1, .cost = units::Money { 100 } }, { .id = 2, .cost = units::Money { 200 } } };
    invent_everything(research, items, 42);
    CHECK(items.empty());
    CHECK(research.invented == std::vector<u64> { 1, 2 });
    CHECK(research.invented_at == std::vector<f32> { 42, 42 });
    CHECK(research.previous == 2);
    CHECK_FALSE(research.current);
    CHECK_FALSE(research.focus);
    CHECK(research.set_aside.empty());
    CHECK(research.spent == units::Money { });
}

TEST_CASE("debug tools: guests come in on the spots given until there are enough, and none without spots")
{
    Sandbox park;
    park.path({ 0, 0 }, { 5, 0 });
    auto& r = park.world().registry;
    random::RandomState dice = random::seeded({ 3 });
    CHECK(add_guests(park.world(), dice, { }, 10) == 0);
    const std::array spots { NodeId { 1 }, NodeId { 4 } };
    const usize came = add_guests(park.world(), dice, spots, 10);
    CHECK(came >= 10);
    CHECK(guests_in(r) == came);
    for (const auto [e, stride] : r.view<const Stride>().each())
        CHECK((stride.from == spots[0] || stride.from == spots[1]));
    park.run_seconds(5);
    CHECK(guests_in(r) == came);
}

TEST_CASE("debug tools: everyone goes home from the queues, the rides and their parties, and the park runs on")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = park.flat_ride("Twister", { 4, 0 }, 0, { .capacity = 2, .cycle_seconds = 30, .excitement = 1, .intensity = 0.3f });
    for (i32 i = 0; i < 6; ++i)
        park.guest({ i % 4, 0 }, { .happiness = 1 });
    (void)park.party({ park.guest({ 0, 0 }), park.guest({ 0, 0 }) });
    auto& r = park.world().registry;
    REQUIRE(park.run_until([&] { return !r.get<CycleRide>(ride).riders.empty() && !r.get<CycleRide>(ride).queue.empty(); }, 120));
    CHECK(send_guests_home(park.world()) == 8);
    CHECK(guests_in(r) == 0);
    CHECK(r.get<CycleRide>(ride).riders.empty());
    CHECK(r.get<CycleRide>(ride).queue.empty());
    CHECK(r.view<Party>().empty());
    park.run_seconds(60);
    CHECK(guests_in(r) == 0);
}

TEST_CASE("debug tools: refreshing guests meets their needs")
{
    Sandbox park;
    park.path({ 0, 0 }, { 2, 0 });
    const auto guest = park.guest({ 0, 0 }, { .thirst = 0.9f, .hunger = 0.8f, .bladder = 0.7f, .happiness = 0.1f, .energy = 0.2f, .nausea = 0.6f });
    refresh_guests(park.world());
    const auto& r = park.world().registry;
    CHECK(r.get<Needs>(guest).thirst.value == 0);
    CHECK(r.get<Needs>(guest).hunger.value == 0);
    CHECK(r.get<Needs>(guest).bladder.value == 0);
    CHECK(r.get<Mood>(guest).happiness.value == 1);
    CHECK(r.get<Mood>(guest).energy.value == 1);
    CHECK(r.get<Mood>(guest).nausea.value == 0);
}

TEST_CASE("debug tools: rides break down all at once, and a reset repairs, empties and zeroes them")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = park.flat_ride("Twister", { 4, 0 }, 0, { .capacity = 2, .cycle_seconds = 30, .excitement = 1, .intensity = 0.3f });
    const auto other = park.flat_ride("Teacups", { 0, 0 }, 0);
    for (i32 i = 0; i < 6; ++i)
        park.guest({ 1 + i % 3, 0 }, { .happiness = 1 });
    auto& r = park.world().registry;
    REQUIRE(park.run_until([&] { return r.get<Attraction>(ride).customers > 0 && !r.get<CycleRide>(ride).queue.empty(); }, 120));
    r.get<Upkeep>(ride).reliability = units::Share { 0.4f };

    CHECK(break_down_rides(park.world()) == 2);
    CHECK(break_down_rides(park.world()) == 0); // Broken already
    for (const auto e : { ride, other })
    {
        CHECK(r.get<Upkeep>(e).broken);
        CHECK(r.get<Upkeep>(e).kind != Breakdown::None);
        CHECK_FALSE(r.get<Attraction>(e).open);
    }

    reset_rides(park.world());
    CHECK_FALSE(r.get<Upkeep>(ride).broken);
    CHECK(r.get<Upkeep>(ride).kind == Breakdown::None);
    CHECK(r.get<Upkeep>(ride).reliability.value == 1);
    CHECK(r.get<Upkeep>(ride).breakdowns == 0);
    CHECK(r.get<Attraction>(ride).open);
    CHECK(r.get<Attraction>(ride).customers == 0);
    CHECK(r.get<CycleRide>(ride).queue.empty());
    for (const auto [e, errand] : r.view<const Errand>().each())
        CHECK(errand.kind != ErrandKind::Queueing);
    park.run_seconds(30);
}

TEST_CASE("debug tools: animals are taken away, keepers after them go back to work, and needs are met")
{
    SimWorld world;
    const entt::entity keeper = add_animal_house(world, { .object = 7, .enclosure = std::optional { 0 } }, { 1, 0, 1 });
    const entt::entity animal = add_animal(world, { .species = "Lion", .enclosure = 0, .job = AnimalJob::Idle, .hunger = units::Share { 0.8f }, .health = units::Share { 0.3f } }, { 2, 0, 2 });
    refresh_animals(world);
    CHECK(world.registry.get<Animal>(animal).hunger.value == 0);
    CHECK(world.registry.get<Animal>(animal).health.value == 1);

    world.registry.get<KeeperDuty>(keeper).job = KeeperJob::Catching;
    world.registry.get<KeeperDuty>(keeper).target = animal;
    CHECK(remove_animals(world) == 1);
    CHECK(world.registry.view<Animal>().empty());
    CHECK(world.registry.get<KeeperDuty>(keeper).job == KeeperJob::Idle);
    CHECK(world.registry.get<KeeperDuty>(keeper).target == entt::null);
}

TEST_CASE("debug tools: every adult female out of her crate falls pregnant, by a male of her kind beside her if any")
{
    SimWorld world;
    const entt::entity male = add_animal(world, { .species = "Lion", .male = true, .enclosure = 0, .job = AnimalJob::Idle }, { 2, 0, 2 });
    const entt::entity female = add_animal(world, { .species = "Lion", .enclosure = 0, .job = AnimalJob::Idle }, { 2, 0, 2 });
    const entt::entity alone = add_animal(world, { .species = "Lion", .enclosure = 1, .job = AnimalJob::Idle }, { 2, 0, 2 });
    const entt::entity young = add_animal(world, { .species = "Lion", .adult = false, .enclosure = 0, .job = AnimalJob::Idle }, { 2, 0, 2 });
    for (const entt::entity e : { male, female, alone, young })
        world.registry.get<Animal>(e).job = AnimalJob::Idle;
    CHECK(make_animals_pregnant(world, units::Seconds { 30 }) == 2);
    CHECK(world.registry.get<Animal>(female).due->value == 30);
    CHECK(world.registry.get<Animal>(female).litter == 1);
    CHECK(world.registry.get<Animal>(female).mate == male);
    CHECK(world.registry.get<Animal>(alone).mate == entt::null);
    CHECK_FALSE(world.registry.get<Animal>(young).due.has_value());
    CHECK_FALSE(world.registry.get<Animal>(male).due.has_value());
    CHECK(make_animals_pregnant(world, units::Seconds { 30 }) == 0);
}

TEST_CASE("debug tools: litter is dropped across the spots given and cleared again")
{
    Sandbox park;
    park.path({ 0, 0 }, { 5, 0 });
    random::RandomState dice = random::seeded({ 5 });
    const std::array spots { NodeId { 0 }, NodeId { 3 } };
    CHECK(scatter_litter(park.world(), dice, { }, 5) == 0);
    CHECK(scatter_litter(park.world(), dice, spots, 20) == 20);
    for (const auto [e, piece] : park.world().registry.view<const Litter>().each())
        CHECK((piece.node == spots[0] || piece.node == spots[1]));
    park.litter({ 5, 0 }, 3);
    CHECK(clear_litter(park.world()) == 23);
    CHECK(park.world().registry.view<Litter>().empty());
}

TEST_CASE("debug tools: aging the animals brings their growing up nearer")
{
    SimWorld world;
    const entt::entity young = add_animal(world, { .species = "Lion", .adult = false, .enclosure = 0 }, { 2, 0, 2 });
    CHECK(age_animals(world, units::Seconds { 300 }) == 1);
    CHECK(world.registry.get<Animal>(young).age.value == 300);
}
