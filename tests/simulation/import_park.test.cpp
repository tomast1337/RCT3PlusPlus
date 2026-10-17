// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/import_park.hpp"

#include "simulation/components.hpp"
#include "simulation/import_people.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3;

TEST_CASE("a flat ride's timing is its starting, running and stopping animations, running as many times as set")
{
    const std::array<float, 4> seconds { 0, 10, 20, 5 }; // Standing, starting, running, stopping
    const auto timing = simulation::flat_ride_timing(seconds, 16, 3);
    REQUIRE(timing);
    CHECK(timing->seats == 16);
    CHECK(timing->seconds == doctest::Approx(75));
    CHECK(simulation::flat_ride_timing(seconds, 0, 1) == std::nullopt);                      // Nowhere to sit
    CHECK(simulation::flat_ride_timing(std::span(seconds).first(3), 16, 1) == std::nullopt); // Not a ride's animations
}

TEST_CASE("a stall sells what its items are named, or what its own kind is named when the items are not")
{
    simulation::SimWorld world;
    for (int c = 0; c <= 3; ++c)
        world.paths.add({ c, 0, 0 });
    save::RideModel rides;
    const auto stall = [&](std::string symbol, std::string item, int col) {
        save::Stall& s = rides.stalls.emplace_back();
        s.base.symbol = std::move(symbol);
        s.base.spot = save::PlacementSpot { .col = col, .row = 1 };
        s.items.push_back({ .price = 120, .name = std::move(item) });
    };
    stall("GenericStall:sta", "Cola", 0);
    stall("HotDogStall:sta", "%ME%", 1);
    stall("SouvenirStall:sta", "%ME%", 2); // Neither: left out
    CHECK(simulation::import_stalls(world, rides) == 2);
    std::vector<simulation::Product> sold;
    for (const auto [e, shop, attraction] : world.registry.view<const simulation::Shop, const simulation::Attraction>().each())
    {
        sold.push_back(shop.product);
        CHECK(attraction.price == 120);
    }
    std::ranges::sort(sold);
    CHECK(sold == std::vector { simulation::Product::Drink, simulation::Product::Food });
}

TEST_CASE("a ride comes over with its queue line, from the entrance back to the paths, and its exit")
{
    simulation::SimWorld world;
    for (int c = 0; c <= 2; ++c)
        world.paths.add({ c, 0, 0 });
    world.paths.add({ 4, 2, 0 });
    // Line 7 runs from (5, 0) by the entrance back to (3, 0) beside the path; line 8 is another ride's.
    for (int c = 3; c <= 5; ++c)
        world.paths.add({ c, 0, 0 }, true, std::nullopt, 7);
    world.paths.add({ 5, 1, 0 }, true, std::nullopt, 8);

    save::RideModel rides;
    save::Ride& ride = rides.rides.emplace_back();
    ride.kind = save::RideKind::Flat;
    ride.base.name = "Spinner";
    ride.ticket_price = 150;
    ride.assessment = { .valid = true, .excitement = 5, .intensity = 2, .nausea = 12 };
    save::Station& station = ride.stations.emplace_back();
    station.entrance_spot = save::PlacementSpot { .col = 6, .row = 0 };
    station.exit_spot = save::PlacementSpot { .col = 4, .row = 1 };
    station.queue_line = 7;
    const std::array<std::optional<simulation::RideTiming>, 1> timings { simulation::RideTiming { .seats = 12, .seconds = 40 } };

    const auto added = simulation::import_rides(world, rides, timings);
    REQUIRE(added.size() == 1);
    const auto e = added[0];
    REQUIRE(world.registry.valid(e));
    const auto& cycle = world.registry.get<simulation::CycleRide>(e);
    const auto at = [&](int c, int r) { return *world.paths.find({ c, r, 0 }); };
    CHECK(cycle.line == std::vector { at(5, 0), at(4, 0), at(3, 0) });
    CHECK(world.registry.get<simulation::Attraction>(e).entrance == at(3, 0));
    CHECK(cycle.exit == at(4, 2));
    CHECK(cycle.capacity == 12);
    CHECK(cycle.excitement == doctest::Approx(0.5f));
    CHECK(cycle.nausea == 1);                                     // Ratings past 10 count as 10
    CHECK_FALSE(world.registry.all_of<simulation::Departure>(e)); // A flat ride's saved settings are not its own
}

TEST_CASE("a tracked ride leaves the station as its saved operating settings say")
{
    save::OperatingOptions o;
    o.wait_for_load = 1;
    o.min_wait = 10;
    o.max_wait = -1;
    const simulation::Departure d = simulation::departure_of(o);
    CHECK(d.wait_for == simulation::LoadWait::ThreeQuarters);
    CHECK(d.min_wait == 10);
    CHECK(d.max_wait == -1);
    o.wait_for_load = 9;
    CHECK(simulation::departure_of(o).wait_for == simulation::LoadWait::Any);
}

TEST_CASE("guests the park had on a ride are put back on it, as many as it seats")
{
    simulation::SimWorld world;
    for (int c = 0; c <= 3; ++c)
        world.paths.add({ c, 0, 0 });
    save::RideModel rides;
    save::Ride& ride = rides.rides.emplace_back();
    ride.kind = save::RideKind::Flat;
    ride.base.id = 42;
    ride.stations.emplace_back().entrance_spot = save::PlacementSpot { .col = 4, .row = 0 };
    const std::array<std::optional<simulation::RideTiming>, 1> timings { simulation::RideTiming { .seats = 2, .seconds = 40 } };
    const auto added = simulation::import_rides(world, rides, timings);
    REQUIRE(world.registry.valid(added[0]));

    // Three up in the air on the ride, far from any path, and one walking by on the path.
    save::PeopleModel people;
    for (int k = 0; k < 4; ++k)
    {
        save::Guest& g = people.guests.emplace_back();
        g.in_park = true;
        g.person.id = static_cast<save::PeopleObjectId>(k + 1);
        g.person.state = k < 3 ? save::PersonState::OnRide : save::PersonState::Wandering;
        g.person.state_target.id = 42;
        g.person.position = k < 3 ? save::PeopleVec3 { 40, 12, 40 } : save::PeopleVec3 { 2, 0, 0 };
    }
    const auto guests = simulation::import_guests(world, people);
    simulation::start_guests(world);
    CHECK(simulation::board_saved_riders(world, people, guests, rides, added) == 2);
    auto& r = world.registry;
    const auto& cycle = r.get<simulation::CycleRide>(added[0]);
    CHECK(cycle.riders == std::vector { guests[0], guests[1] });
    CHECK(cycle.timer > 0);
    CHECK(r.get<simulation::Errand>(guests[0]).kind == simulation::ErrandKind::Riding);
    // No room for the third: they walk off from the ride; the one on the path walks on.
    CHECK(r.all_of<simulation::Stride>(guests[2]));
    CHECK(r.get<simulation::Errand>(guests[2]).kind != simulation::ErrandKind::Riding);
    CHECK(r.get<simulation::Errand>(guests[3]).kind != simulation::ErrandKind::Riding);
}
