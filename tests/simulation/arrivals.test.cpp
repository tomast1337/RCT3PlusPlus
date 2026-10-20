// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/components.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"

#include <doctest/doctest.h>

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
    park.world().registry.get<Attraction>(ride).price = 0;
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
