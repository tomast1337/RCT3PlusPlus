// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/debug_tools.hpp"

#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/parties.hpp"
#include "simulation/rides.hpp"
#include "simulation/sandbox.hpp"

#include <array>
#include <doctest/doctest.h>
#include <ranges>

using namespace rct3;
using namespace rct3::simulation;

namespace
{
    std::size_t guests_in(const entt::registry& r)
    {
        return static_cast<std::size_t>(std::ranges::distance(r.view<const Visitor>(entt::exclude<Left>)));
    }
} // namespace

TEST_CASE("debug tools: finishing research invents everything left at once")
{
    Research research { .spent = units::Money { 500 }, .current = 2, .focus = 2, .unfocused = 1, .set_aside = { { .id = 1, .spent = units::Money { 40 } } } };
    std::vector<ResearchItem> items { { .id = 1, .cost = units::Money { 100 } }, { .id = 2, .cost = units::Money { 200 } } };
    invent_everything(research, items, 42);
    CHECK(items.empty());
    CHECK(research.invented == std::vector<std::uint64_t> { 1, 2 });
    CHECK(research.invented_at == std::vector<float> { 42, 42 });
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
    const std::size_t came = add_guests(park.world(), dice, spots, 10);
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
    for (int i = 0; i < 6; ++i)
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
