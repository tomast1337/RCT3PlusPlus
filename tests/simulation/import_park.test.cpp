// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/import_park.hpp"

#include "simulation/components.hpp"

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
    CHECK(cycle.nausea == 1); // Ratings past 10 count as 10
}
