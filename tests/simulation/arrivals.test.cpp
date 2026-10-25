// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/components.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <utility>

using namespace rct3::simulation;

TEST_CASE("the park has room for guests on its working rides and in its pools")
{
    Sandbox park;
    park.path({ 0, 0 }, { 6, 0 });
    const auto spinner = park.flat_ride("Spinner", { 1, 0 }, 100, { .capacity = 10 });
    const auto swing = park.flat_ride("Swing", { 3, 0 }, 100, { .capacity = 10 });
    auto& r = park.world().registry;
    const std::uint32_t both = ideal_guests(park.world());
    CHECK(both > 0);
    r.emplace_or_replace<Upkeep>(swing, Upkeep { .broken = true });
    CHECK(ideal_guests(park.world()) == both / 2);
    r.get<Attraction>(spinner).open = false;
    CHECK(ideal_guests(park.world()) == 0);
    r.ctx().emplace<Draw>(Draw { .pool_squares = 25 });
    CHECK(ideal_guests(park.world()) > 25);
    const std::uint32_t pools = ideal_guests(park.world());
    r.ctx().emplace<RoomElsewhere>(RoomElsewhere { 40 }); // Rides not simulated
    CHECK(ideal_guests(park.world()) > pools + 40);
}

TEST_CASE("cheaper rides make a higher entrance fee fair")
{
    Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    const auto ride = park.flat_ride("Spinner", { 1, 0 }, 300, { .excitement = 0.8f });
    const std::int64_t dear = fair_entrance_fee(park.world());
    park.world().registry.get<Attraction>(ride).price.value = 0;
    CHECK(fair_entrance_fee(park.world()) > dear);
}

TEST_CASE("a well rated park with room draws more guests; a full or overpriced one fewer")
{
    const Draw plain { };
    CHECK(drawn(plain, 0, 100, 0) == doctest::Approx(1));
    CHECK(drawn({ .rating = 0.9f }, 0, 100, 0) > 1);
    CHECK(drawn({ .rating = 0.2f }, 0, 100, 0) < drawn({ .rating = 0.4f }, 0, 100, 0));
    CHECK(drawn({ .rating = 0.2f }, 0, 100, 0) > 0); // A few still come to a park nobody rates
    CHECK(drawn(plain, 100, 100, 0) < 1);
    const double fair = drawn({ .entrance_fee = 1000 }, 0, 100, 1000);
    CHECK(drawn({ .entrance_fee = 1500 }, 0, 100, 1000) < fair);
    CHECK(drawn({ .entrance_fee = 2500 }, 0, 100, 1000) < drawn({ .entrance_fee = 1500 }, 0, 100, 1000));
}

TEST_CASE("a park with room for many guests draws them in faster")
{
    const auto came = [](std::uint32_t pool_squares) {
        Sandbox park;
        park.path({ 0, 0 }, { 3, 0 });
        auto& r = park.world().registry;
        r.emplace<ParkGate>(r.create(), ParkGate { park.world().paths.find({ 0, 0, 0 }).value() });
        r.ctx().emplace<Draw>(Draw { .pool_squares = pool_squares });
        park.run_seconds(120);
        const auto& in = r.ctx().emplace<Turnstile>().in;
        return in[0] + in[1] + in[2];
    };
    CHECK(came(1000) > 3 * came(10));
}

TEST_CASE("guests come in parties: children only ever with an adult, each party led by its first member")
{
    Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    auto& r = park.world().registry;
    r.emplace<ParkGate>(r.create(), ParkGate { park.world().paths.find({ 0, 0, 0 }).value() });
    park.run_seconds(1800);
    std::size_t children = 0;
    for (const auto [e, visitor] : r.view<const Visitor>(entt::exclude<Left>).each())
    {
        if (visitor.age != AgeGroup::Child)
            continue;
        ++children;
        const auto* member = r.try_get<Member>(e);
        REQUIRE(member != nullptr);
        const Party& party = r.get<Party>(member->party);
        CHECK(std::ranges::any_of(party.members, [&](entt::entity m) { return r.get<Visitor>(m).age == AgeGroup::Adult; }));
        CHECK(r.get<Visitor>(party.leader).age == AgeGroup::Adult);
    }
    CHECK(children > 0);
    CHECK(r.view<const Party>().size() > 3);
}

TEST_CASE("wild rides draw teens' parties, gentle ones families; a park's own mix is followed")
{
    const auto weights = [](float intensity) {
        Sandbox park;
        park.path({ 0, 0 }, { 3, 0 });
        park.flat_ride("Ride", { 1, 0 }, 100, { .intensity = intensity });
        return party_weights(park.world(), false);
    };
    const auto wild = weights(1);
    const auto gentle = weights(0);
    const auto teens = std::to_underlying(PartyKind::TeenGang);
    const auto families = std::to_underlying(PartyKind::Family);
    CHECK(wild[teens] > gentle[teens]);
    CHECK(wild[families] < gentle[families]);

    Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    auto& r = park.world().registry;
    r.emplace<ParkGate>(r.create(), ParkGate { park.world().paths.find({ 0, 0, 0 }).value() });
    PartyMix mix;
    mix.weights[std::to_underlying(PartyKind::DayTrip)] = 1;
    r.ctx().emplace<PartyMix>(mix);
    park.run_seconds(600);
    const auto& in = r.ctx().emplace<Turnstile>().in;
    CHECK(in[std::to_underlying(AgeGroup::Child)] > 0);
    CHECK(in[std::to_underlying(AgeGroup::Teen)] == 0);
    for (const auto [e, party] : r.view<const Party>().each())
        CHECK(r.get<Visitor>(party.leader).age == AgeGroup::Adult);
}

TEST_CASE("nobody comes in while the park is closed")
{
    Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    auto& r = park.world().registry;
    r.emplace<ParkGate>(r.create(), ParkGate { .node = park.world().paths.find({ 0, 0, 0 }).value(), .closed = true });
    park.run_seconds(600);
    CHECK(r.view<const Visitor>().empty());
}
