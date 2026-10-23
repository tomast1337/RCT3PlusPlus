// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/components.hpp"
#include "simulation/import_park.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3::simulation;

TEST_CASE("guests use toilets, cash machines and first aid when they need them")
{
    {
        Sandbox park;
        park.path({ 0, 0 }, { 6, 0 });
        const auto toilet = park.shop("Toilet", Product::Toilet, { 6, 0 }, 20);
        const auto g = park.guest({ 0, 0 }, { .bladder = 0.9f });
        auto& r = park.world().registry;
        CHECK(park.run_until([&] { return r.get<Needs>(g).bladder.value < 0.1f; }, 60));
        CHECK(r.get<Attraction>(toilet).customers == 1);
        CHECK(r.get<Wallet>(g).cash.value == 5000 - 20);
        CHECK_FALSE(r.all_of<Rubbish>(g)); // Nothing to throw away
    }
    {
        Sandbox park;
        park.path({ 0, 0 }, { 6, 0 });
        park.shop("Cash", Product::CashMachine, { 6, 0 }, 0);
        const auto g = park.guest({ 0, 0 }, { .cash = 100 });
        CHECK(park.run_until([&] { return park.world().registry.get<Wallet>(g).cash.value > 100; }, 60));
    }
    {
        Sandbox park;
        park.path({ 0, 0 }, { 6, 0 });
        park.shop("First aid", Product::FirstAid, { 6, 0 }, 0);
        const auto g = park.guest({ 0, 0 }, { .nausea = 0.9f });
        CHECK(park.run_until([&] { return park.world().registry.get<Mood>(g).nausea.value == 0; }, 60));
    }
}

TEST_CASE("tired guests sit on the benches they pass, as many as there are seats, and rest")
{
    Sandbox park;
    park.path({ 0, 0 }, { 1, 0 });
    park.item(PathItemKind::Bench, { 0, 0 });
    std::vector<entt::entity> guests;
    for (int i = 0; i < 6; ++i)
        guests.push_back(park.guest({ 1, 0 }, { .energy = 0.1f }));
    auto& r = park.world().registry;
    std::size_t most = 0;
    for (int t = 0; t < 400; ++t)
    {
        park.run(1);
        most = std::max<std::size_t>(most, r.view<Linger>().size());
    }
    CHECK(most > 0);
    CHECK(most <= 3);
    CHECK(std::ranges::any_of(guests, [&](entt::entity g) { return r.get<Mood>(g).energy.value > 0.2f; }));
}

TEST_CASE("guests stop to watch a sight, cheered by it, and vandals leave it be")
{
    Sandbox park;
    park.path({ 0, 0 }, { 1, 0 });
    const auto fountain = park.item(PathItemKind::Sight, { 0, 0 });
    const auto g = park.guest({ 1, 0 }, { .happiness = 0.3f });
    auto& r = park.world().registry;
    CHECK(park.run_until([&] { return r.all_of<Linger>(g); }, 120));
    CHECK(r.get<Linger>(g).at == PathItemKind::Sight);
    const float before = r.get<Mood>(g).happiness.value;
    park.run_seconds(10);
    CHECK(r.get<Mood>(g).happiness.value > before);
    r.get<Mood>(g).happiness.value = 0;
    park.run_seconds(600);
    CHECK_FALSE(r.get<PathItem>(fountain).broken);
}

TEST_CASE("a party with the player's guest in it has its photo taken at a photo spot, others do not")
{
    for (const bool avatar : { true, false })
    {
        Sandbox park;
        park.path({ 0, 0 }, { 1, 0 });
        park.item(PathItemKind::PhotoSpot, { 0, 0 });
        const auto leader = park.guest({ 1, 0 });
        const auto friend_ = park.guest({ 1, 0 });
        park.party({ leader, friend_ });
        auto& r = park.world().registry;
        r.get<Visitor>(friend_).avatar = avatar;
        CHECK(park.run_until([&] { return r.all_of<Linger>(leader); }, 120));
        CHECK(r.get<Linger>(leader).at == PathItemKind::PhotoSpot);
        const auto* photos = r.ctx().find<Photos>();
        if (avatar)
        {
            REQUIRE(photos != nullptr);
            REQUIRE(photos->taken.size() == 1);
            CHECK(photos->taken[0].guest == friend_);
        }
        else
            CHECK((photos == nullptr || photos->taken.empty()));
    }
}

TEST_CASE("special attractions are told apart by the kind their item stores")
{
    CHECK(facility_of(11) == Product::Toilet);
    CHECK(facility_of(12) == Product::CashMachine);
    CHECK(facility_of(13) == Product::FirstAid);
    CHECK(facility_of(14) == std::nullopt);
}
