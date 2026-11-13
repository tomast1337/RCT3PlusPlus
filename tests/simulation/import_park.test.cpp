// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/import_park.hpp"

#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/import_people.hpp"
#include "simulation/rides.hpp"
#include "simulation/stalls.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <array>
#include <cmath>
#include <doctest/doctest.h>

using namespace rct3;

TEST_CASE("a show ride's go lasts as long as the tricks of its show")
{
    simulation::CycleRide ride { .start = { 3 }, .run = { 10 }, .stop = { 2 }, .loops = 4 };
    const save::model::RideShow show { .ride = 1, .tricks = { { .name = "a", .seconds = 5 }, { .name = "b", .seconds = 20 }, { .name = "c", .seconds = 7 } }, .order = { 1, 2, 1, 9 } }; // 9: no such trick
    simulation::stage_show(ride, show);
    CHECK(simulation::cycle_seconds(ride) == 47);
    CHECK(ride.loops == 1);
}

TEST_CASE("a flat ride's timing is its starting, running and stopping animations, running as many times as set")
{
    const std::array<float, 4> seconds { 0, 10, 20, 5 }; // Standing, starting, running, stopping
    const auto timing = simulation::flat_ride_timing(seconds, 16, 3);
    REQUIRE(timing);
    CHECK(timing->seats == 16);
    CHECK(timing->start == 10);
    CHECK(timing->run == 20);
    CHECK(timing->stop == 5);
    CHECK(timing->loops == 3);
    CHECK(simulation::flat_ride_timing(seconds, 16, 0)->loops == 1);                       // Runs at least once
    CHECK(simulation::flat_ride_timing(seconds, 0, 1) == std::nullopt);                    // Nowhere to sit
    CHECK(simulation::flat_ride_timing(std::array { 0.0f, 0.0f }, 16, 1) == std::nullopt); // Nothing that takes time
}

TEST_CASE("a ride without a starting, running and stopping cycle runs once for its longest animation")
{
    // A maze's standing animation alone takes time; a show lists two.
    const auto maze = simulation::flat_ride_timing(std::array { 10.0f, 0.0f, 0.0f, 0.0f }, 2, 3);
    REQUIRE(maze);
    CHECK(maze->start == 0);
    CHECK(maze->run == 10);
    CHECK(maze->stop == 0);
    CHECK(maze->loops == 1);
    CHECK(simulation::flat_ride_timing(std::array { 0.5f, 30.0f }, 24, 1)->run == 30);
}

TEST_CASE("a stall sells what its items are named, or their kinds are, or its own kind is when neither tells")
{
    simulation::SimWorld world;
    for (int c = 0; c <= 4; ++c)
        world.paths.add({ c, 0, 0 });
    save::model::RideModel rides;
    const auto stall = [&](std::string symbol, std::string item, int col, std::string kind = { }) {
        save::model::Stall& s = rides.stalls.emplace_back();
        s.base.symbol = std::move(symbol);
        s.base.spot = save::model::PlacementSpot { .col = col, .row = 1 };
        s.items.push_back({ .kind_symbol = std::move(kind), .price = 120, .name = std::move(item) });
    };
    stall("GenericStall:sta", "Cola", 0);
    stall("HotDogStall:sta", "%ME%", 1);
    stall("SouvenirStall:sta", "%ME%", 2);                // Neither: left out
    stall("GenericStall:sta", "%ME%", 3, "RootBeer:cid"); // A custom stall, told by its item's kind
    CHECK(simulation::import_stalls(world, rides) == 3);
    std::vector<simulation::Product> sold;
    for (const auto [e, shop, attraction] : world.registry.view<const simulation::Shop, const simulation::Attraction>().each())
    {
        sold.push_back(shop.product);
        CHECK(attraction.price.value == 120);
    }
    std::ranges::sort(sold);
    CHECK(sold == std::vector { simulation::Product::Drink, simulation::Product::Drink, simulation::Product::Food });
}

TEST_CASE("a stall's item is food or drink by what its kind feeds or quenches, whatever it is called")
{
    simulation::SimWorld world;
    world.paths.add({ 0, 0, 0 });
    world.paths.add({ 1, 0, 0 });
    const park::CarriedKinds kinds { .items = { { .symbol = "Nectar:cid", .effects = { 0, 0.3f, 0 } }, { .symbol = "Crumble:cid", .effects = { 0.2f, -0.1f, 0 } } } };
    save::model::RideModel rides;
    for (const auto& [kind, col] : { std::pair { "Nectar:cid", 0 }, std::pair { "Crumble:cid", 1 } })
    {
        save::model::Stall& s = rides.stalls.emplace_back();
        s.base.symbol = "Odd:sta";
        s.base.spot = save::model::PlacementSpot { .col = col, .row = 1 };
        s.items.push_back({ .kind_symbol = kind, .price = 100, .name = "%ME%" });
    }
    REQUIRE(simulation::import_stalls(world, rides, kinds) == 2);
    std::vector<simulation::Product> sold;
    for (const auto [_, shop] : world.registry.view<const simulation::Shop>().each())
        sold.push_back(shop.product);
    std::ranges::sort(sold);
    CHECK(sold == std::vector { simulation::Product::Drink, simulation::Product::Food });
}

TEST_CASE("a stall's food and drink is cold or hot by its name, and plain otherwise")
{
    simulation::SimWorld world;
    for (int c = 0; c <= 3; ++c)
        world.paths.add({ c, 0, 0 });
    save::model::RideModel rides;
    for (const auto& [item, col] : { std::pair { "Ice Cream", 0 }, std::pair { "Hot Chocolate", 1 }, std::pair { "Steak", 2 }, std::pair { "Milk Shake", 3 } })
    {
        save::model::Stall& s = rides.stalls.emplace_back();
        s.base.spot = save::model::PlacementSpot { .col = col, .row = 1 };
        s.items.push_back({ .price = 100, .name = item });
    }
    REQUIRE(simulation::import_stalls(world, rides) == 4);
    std::vector<simulation::Serve> served;
    for (const auto [_, shop] : world.registry.view<const simulation::Shop>().each())
        served.push_back(shop.serve);
    std::ranges::sort(served);
    CHECK(served == std::vector { simulation::Serve::Any, simulation::Serve::Cold, simulation::Serve::Cold, simulation::Serve::Hot });
}

TEST_CASE("a stall's ware takes the extras ticked on it or left to the guest, as much as the stall puts on, and their cost")
{
    simulation::SimWorld world;
    world.paths.add({ 0, 0, 0 });
    const park::CarriedKinds kinds {
        .items = { { .symbol = "Burger:cid", .extras = { "Ketchup:ced", "Cheese:ced", "Onion:ced" }, .cost = 50, .worth = { 200, 240, 170 } } },
        .extras = { { .symbol = "Ketchup:ced", .effects = { 0.1f, 0, 0 } }, { .symbol = "Cheese:ced", .effects = { 0.2f, -0.1f, 0 }, .cost = 4 }, { .symbol = "Onion:ced", .effects = { 0.4f, 0, 0 } } },
    };
    save::model::RideModel rides;
    save::model::Stall& s = rides.stalls.emplace_back();
    s.base.spot = save::model::PlacementSpot { .col = 0, .row = 1 };
    s.items.push_back({ .kind_symbol = "Burger:cid", .price = 200, .name = "%ME%", .extras = 0b011 }); // Ketchup and cheese ticked
    s.extras = { { .kind_symbol = "ketchup:ced", .level = save::model::StallExtra::kNone }, { .kind_symbol = "Cheese:ced", .level = 4 }, { .kind_symbol = "Onion:ced", .level = save::model::StallExtra::kGuestChoice } };
    REQUIRE(simulation::import_stalls(world, rides, kinds) == 1);
    const auto& menu = world.registry.get<simulation::Shop>(world.registry.view<simulation::Shop>().front()).menu;
    REQUIRE(menu.size() == 1);
    const simulation::Ware& w = menu.front();
    const auto& amount = simulation::kTuning.eating.extra_amounts;
    CHECK(w.extras == 3);
    CHECK(w.topped == 2); // Ketchup ticked but not offered; onion as the guest likes though not ticked
    CHECK(w.topping[0] == doctest::Approx(0.2f * amount[4] + 0.4f * amount[5]));
    CHECK(w.topping[1] == doctest::Approx(-0.1f * amount[4]));
    // The cheese costs the stall, and is worth that much more to guests.
    const std::int64_t cheese = std::llround(4 * amount[4]);
    CHECK(w.cost.value == 50 + cheese);
    CHECK(w.worth[0].value == 200 + cheese);
    CHECK(w.worth[1].value == 240 + cheese);
    CHECK(w.worth[2].value == 170 + cheese);
    simulation::restock(world, s, { });
    CHECK(world.registry.get<simulation::Shop>(world.registry.view<simulation::Shop>().front()).menu.front().extras == 0); // Kinds unknown: no extras
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

    save::model::RideModel rides;
    save::model::Ride& ride = rides.rides.emplace_back();
    ride.kind = save::model::RideKind::Flat;
    ride.base.name = "Spinner";
    ride.base.symbol = "BumperBoats:ffr"; // Boats, which splash their riders
    ride.ticket_price = { 150 };
    ride.assessment = { .valid = true, .excitement = 5, .intensity = 2, .nausea = 12 };
    save::model::Station& station = ride.stations.emplace_back();
    station.entrance_spot = save::model::PlacementSpot { .col = 6, .row = 0, .direction = 2 }; // Its back to (5, 0)
    station.exit_spot = save::model::PlacementSpot { .col = 4, .row = 1, .direction = 3 };     // Its back to (4, 2)
    station.queue_line = 7;
    const std::array<std::optional<simulation::RideTiming>, 1> timings { simulation::RideTiming { .seats = 12, .run = 40 } };

    const auto added = simulation::import_rides(world, rides, timings);
    REQUIRE(added.size() == 1);
    const auto e = added[0];
    REQUIRE(world.registry.valid(e));
    const auto& cycle = world.registry.get<simulation::CycleRide>(e);
    CHECK(cycle.splash);
    const auto at = [&](int c, int r) { return *world.paths.find({ c, r, 0 }); };
    CHECK(cycle.line == std::vector { at(5, 0), at(4, 0), at(3, 0) });
    CHECK(world.registry.get<simulation::Attraction>(e).entrance == at(3, 0));
    CHECK(cycle.exit == at(4, 2));
    CHECK(cycle.capacity == 12);
    CHECK(cycle.excitement.value == doctest::Approx(0.5f));
    CHECK(cycle.nausea.value == 1);                               // Ratings past 10 count as 10
    CHECK_FALSE(world.registry.all_of<simulation::Departure>(e)); // A flat ride's saved settings are not its own
}

TEST_CASE("a ride's door joins only the path on the tile behind it, near its height")
{
    // Facings 0 to 3 put a door's back towards +col, -row, -col and +row.
    constexpr std::array<std::array<int, 2>, 4> kBehind { { { 1, 0 }, { 0, -1 }, { -1, 0 }, { 0, 1 } } };
    for (std::uint8_t facing = 0; facing < 4; ++facing)
    {
        CAPTURE(facing);
        const auto [bc, br] = kBehind[facing];
        simulation::SimWorld world;
        world.paths.add({ 2 + bc, 2 + br, 0 });
        // Round the exit at (8, 8): paths on its other three sides, and one behind it three levels up.
        for (std::uint8_t side = 1; side < 4; ++side)
            world.paths.add({ 8 + kBehind[(facing + side) % 4][0], 8 + kBehind[(facing + side) % 4][1], 0 });
        world.paths.add({ 8 + bc, 8 + br, 3 });

        save::model::RideModel rides;
        save::model::Ride& ride = rides.rides.emplace_back();
        ride.kind = save::model::RideKind::Flat;
        ride.base.id = 5;
        save::model::Station& station = ride.stations.emplace_back();
        station.entrance_spot = save::model::PlacementSpot { .col = 2, .row = 2, .direction = facing };
        station.exit_spot = save::model::PlacementSpot { .col = 8, .row = 8, .direction = facing };
        const std::array<std::optional<simulation::RideTiming>, 1> timings { simulation::RideTiming { .seats = 4, .run = 30 } };
        const auto e = simulation::import_rides(world, rides, timings).front();
        REQUIRE(world.registry.valid(e));
        CHECK(world.registry.get<simulation::Attraction>(e).entrance == *world.paths.find({ 2 + bc, 2 + br, 0 }));
        CHECK(world.registry.get<simulation::CycleRide>(e).exit == simulation::kNoNode);

        // A path laid behind it a level up, as a ramp's middle may lie, joins it.
        const auto behind = world.paths.add({ 8 + bc, 8 + br, 1 });
        CHECK(simulation::relink_attractions(world, rides) == 1);
        CHECK(world.registry.get<simulation::CycleRide>(e).exit == behind);
    }
}

TEST_CASE("a stall joins only the path on the tile in front of it, near its height")
{
    // Facings 0 to 3 put a stall's front towards -row, -col, +row and +col.
    constexpr std::array<std::array<int, 2>, 4> kFront { { { 0, -1 }, { -1, 0 }, { 0, 1 }, { 1, 0 } } };
    for (std::uint8_t facing = 0; facing < 4; ++facing)
    {
        CAPTURE(facing);
        const auto [fc, fr] = kFront[facing];
        simulation::SimWorld world;
        // Paths on its other three sides, and one in front three levels up.
        for (std::uint8_t side = 1; side < 4; ++side)
            world.paths.add({ 5 + kFront[(facing + side) % 4][0], 5 + kFront[(facing + side) % 4][1], 0 });
        world.paths.add({ 5 + fc, 5 + fr, 3 });
        save::model::RideModel rides;
        save::model::Stall& stall = rides.stalls.emplace_back();
        stall.base.id = 7;
        stall.base.facing = facing;
        stall.base.spot = save::model::PlacementSpot { .col = 5, .row = 5 };
        stall.items.push_back({ .price = 100, .name = "Cola" });
        CHECK(simulation::import_stalls(world, rides) == 0);

        // A path laid in front a level up, as a ramp's middle may lie, joins it.
        const auto front = world.paths.add({ 5 + fc, 5 + fr, 1 });
        REQUIRE(simulation::import_stalls(world, rides) == 1);
        CHECK(world.registry.get<simulation::Attraction>(world.registry.view<simulation::Shop>().front()).entrance == front);
    }
}

TEST_CASE("a toilet or other facility joins a path on any side of it, near its height")
{
    for (const auto [dc, dr] : { std::pair { 1, 0 }, std::pair { 0, -1 }, std::pair { -1, 0 }, std::pair { 0, 1 } })
    {
        CAPTURE(dc);
        CAPTURE(dr);
        simulation::SimWorld world;
        world.paths.add({ 5 + dc, 5 + dr, 3 });
        world.paths.add({ 6, 6, 0 }); // Corner to corner: not beside it
        save::model::RideModel rides;
        save::model::Ride& toilet = rides.rides.emplace_back();
        toilet.kind = save::model::RideKind::Special;
        toilet.base.spot = save::model::PlacementSpot { .col = 5, .row = 5 };
        const auto kind_of = [](const save::model::Ride&) -> std::optional<std::uint32_t> { return 11; };
        CHECK(simulation::import_facilities(world, rides, kind_of) == 0);
        world.paths.add({ 5 + dc, 5 + dr, 0 });
        CHECK(simulation::import_facilities(world, rides, kind_of) == 1);
    }
}

TEST_CASE("a facility follows the paths beside it as they change; one pulled down is left out")
{
    simulation::SimWorld world;
    const auto west = world.paths.add({ 4, 5, 0 });
    save::model::RideModel rides;
    save::model::Ride& toilet = rides.rides.emplace_back();
    toilet.kind = save::model::RideKind::Special;
    toilet.base.id = 9;
    toilet.base.spot = save::model::PlacementSpot { .col = 5, .row = 5 };
    const auto kind_of = [](const save::model::Ride&) -> std::optional<std::uint32_t> { return 11; };
    REQUIRE(simulation::import_facilities(world, rides, kind_of) == 1);
    const auto e = world.registry.view<simulation::Shop>().front();
    CHECK(world.registry.get<simulation::Attraction>(e).entrance == west);

    // Its path taken away and one laid on another side: it serves from that one. A corner path never counts.
    world.paths.remove(west);
    world.paths.add({ 6, 6, 0 });
    CHECK(simulation::relink_attractions(world, rides) == 0);
    const auto south = world.paths.add({ 5, 6, 0 });
    CHECK(simulation::relink_attractions(world, rides) == 1);
    CHECK(world.registry.get<simulation::Attraction>(e).entrance == south);

    rides.rides.front().base.demolished = true;
    CHECK(simulation::import_facilities(world, rides, kind_of) == 0);
}

TEST_CASE("a path laid to a busy ride's exit lets its riders out at once; its way in waits for the queue")
{
    simulation::SimWorld world;
    const auto in = world.paths.add({ 0, 1, 0 });
    save::model::RideModel rides;
    save::model::Ride& ride = rides.rides.emplace_back();
    ride.kind = save::model::RideKind::Flat;
    ride.base.id = 6;
    save::model::Station& station = ride.stations.emplace_back();
    station.entrance_spot = save::model::PlacementSpot { .col = 1, .row = 1, .direction = 2 }; // Its back to (0, 1)
    station.exit_spot = save::model::PlacementSpot { .col = 3, .row = 1, .direction = 0 };     // Its back to (4, 1)
    const std::array<std::optional<simulation::RideTiming>, 1> timings { simulation::RideTiming { .seats = 4, .run = 30 } };
    const auto e = simulation::import_rides(world, rides, timings).front();
    REQUIRE(world.registry.valid(e));
    auto& cycle = world.registry.get<simulation::CycleRide>(e);
    CHECK(cycle.exit == simulation::kNoNode);

    // Someone queues; the path to the entrance is laid again and one is laid to the exit.
    cycle.queue.push_back(world.registry.create());
    world.paths.remove(in);
    const auto again = world.paths.add({ 0, 1, 0 });
    const auto out = world.paths.add({ 4, 1, 0 });
    CHECK(simulation::relink_attractions(world, rides) == 1);
    CHECK(cycle.exit == out);
    CHECK(world.registry.get<simulation::Attraction>(e).entrance == in);

    cycle.queue.clear();
    CHECK(simulation::relink_attractions(world, rides) == 1);
    CHECK(world.registry.get<simulation::Attraction>(e).entrance == again);
}

TEST_CASE("a tracked ride leaves the station as its saved operating settings say")
{
    save::model::OperatingOptions o;
    o.wait_for_load = 1;
    o.min_wait = 10;
    o.max_wait = -1;
    const simulation::Departure d = simulation::departure_of(o);
    CHECK(d.wait_for == simulation::LoadWait::ThreeQuarters);
    CHECK(d.min_wait->value == 10);
    CHECK_FALSE(d.max_wait);
    o.wait_for_load = 9;
    CHECK(simulation::departure_of(o).wait_for == simulation::LoadWait::Any);
}

TEST_CASE("boats for hire carry their riders out and back, though their dock is no circuit")
{
    simulation::SimWorld world;
    for (int c = 0; c <= 3; ++c)
        world.paths.add({ c, 0, 0 });
    save::model::RideModel rides;
    save::model::Ride& ride = rides.rides.emplace_back();
    ride.kind = save::model::RideKind::Tracked;
    ride.mode = save::model::OperatingMode::Boat;
    ride.seats.resize(8); // Four boats of two
    ride.stations.emplace_back().entrance_spot = save::model::PlacementSpot { .col = 4, .row = 0, .direction = 2 };
    const auto added = simulation::import_rides(world, rides, { });
    REQUIRE(world.registry.valid(added[0]));
    const auto& cycle = world.registry.get<simulation::CycleRide>(added[0]);
    CHECK(cycle.capacity == 8);
    CHECK(cycle.run.value == simulation::kTuning.boats.trip.value);
    CHECK_FALSE(world.registry.all_of<simulation::Trains>(added[0]));
}

TEST_CASE("a coaster tested with no path to its doors runs its train, and takes no riders the park had on it")
{
    simulation::SimWorld world;
    world.paths.add({ 0, 0, 0 });
    save::model::RideModel rides;
    save::model::Ride& ride = rides.rides.emplace_back();
    ride.kind = save::model::RideKind::Tracked;
    ride.base.id = 7;
    ride.base.state = save::model::AttractionState::Testing;
    ride.seats.resize(4);
    ride.trains.emplace_back();
    ride.stations.emplace_back().entrance_spot = save::model::PlacementSpot { .col = 9, .row = 9 };
    simulation::Coaster circuit;
    circuit.distance = { 0, 10, 40, 70, 100 };
    circuit.height = { 0, 0, 15, 0, 0 };
    circuit.kinds = { simulation::Stretch::Station, simulation::Stretch::Chain, simulation::Stretch::Plain, simulation::Stretch::Plain, simulation::Stretch::Plain };
    const std::array<std::optional<simulation::Coaster>, 1> coasters { circuit };
    auto added = simulation::import_rides(world, rides, { }, coasters);
    REQUIRE(world.registry.valid(added[0]));
    CHECK(world.registry.get<simulation::Attraction>(added[0]).entrance == simulation::kNoNode);
    CHECK(world.registry.get<simulation::Trains>(added[0]).trains.size() == 1);

    save::model::PeopleModel people;
    save::model::Guest& g = people.guests.emplace_back();
    g.in_park = true;
    g.person.id = 1;
    g.person.state = save::model::PersonState::OnRide;
    g.person.state_target.id = 7;
    const auto guests = simulation::import_guests(world, people);
    CHECK(simulation::board_saved_riders(world, people, guests, rides, added) == 0);

    // Closed, it is left for the paths to reach, as before.
    ride.base.state = save::model::AttractionState::Closed;
    simulation::SimWorld other;
    other.paths.add({ 0, 0, 0 });
    CHECK(simulation::import_rides(other, rides, { }, coasters)[0] == entt::null);
}

TEST_CASE("guests the park had on a ride are put back on it, as many as it seats")
{
    simulation::SimWorld world;
    for (int c = 0; c <= 3; ++c)
        world.paths.add({ c, 0, 0 });
    save::model::RideModel rides;
    save::model::Ride& ride = rides.rides.emplace_back();
    ride.kind = save::model::RideKind::Flat;
    ride.base.id = 42;
    ride.stations.emplace_back().entrance_spot = save::model::PlacementSpot { .col = 4, .row = 0, .direction = 2 };
    const std::array<std::optional<simulation::RideTiming>, 1> timings { simulation::RideTiming { .seats = 2, .run = 40 } };
    const auto added = simulation::import_rides(world, rides, timings);
    REQUIRE(world.registry.valid(added[0]));

    // Three up in the air on the ride, far from any path, and one walking by on the path.
    save::model::PeopleModel people;
    for (int k = 0; k < 4; ++k)
    {
        save::model::Guest& g = people.guests.emplace_back();
        g.in_park = true;
        g.person.id = static_cast<save::model::PeopleObjectId>(k + 1);
        g.person.state = k < 3 ? save::model::PersonState::OnRide : save::model::PersonState::Wandering;
        g.person.state_target.id = 42;
        g.person.position = k < 3 ? save::model::PeopleVec3 { 40, 12, 40 } : save::model::PeopleVec3 { 2, 0, 0 };
    }
    const auto guests = simulation::import_guests(world, people);
    simulation::start_guests(world);
    CHECK(simulation::board_saved_riders(world, people, guests, rides, added) == 2);
    auto& r = world.registry;
    const auto& cycle = r.get<simulation::CycleRide>(added[0]);
    CHECK(cycle.riders == std::vector { guests[0], guests[1] });
    CHECK(cycle.phase == simulation::RidePhase::Running);
    CHECK(r.get<simulation::Errand>(guests[0]).kind == simulation::ErrandKind::Riding);
    // No room for the third: they walk off from the ride; the one on the path walks on.
    CHECK(r.all_of<simulation::Stride>(guests[2]));
    CHECK(r.get<simulation::Errand>(guests[2]).kind != simulation::ErrandKind::Riding);
    CHECK(r.get<simulation::Errand>(guests[3]).kind != simulation::ErrandKind::Riding);
}

TEST_CASE("a guest saved off the paths or on a queue no ride owns walks from the nearest path")
{
    simulation::SimWorld world;
    const auto path = world.paths.add({ 0, 0, 0 });
    const auto queue = world.paths.add({ 1, 0, 0 }, true);
    save::model::PeopleModel people;
    for (const glm::vec3 at : { world.paths.centre(queue), glm::vec3(60, 0, 60) })
    {
        save::model::Guest& g = people.guests.emplace_back();
        g.in_park = true;
        g.person.id = static_cast<save::model::PeopleObjectId>(people.guests.size());
        g.person.state = save::model::PersonState::Swimming;
        g.person.position = { at.x, at.y, at.z };
    }
    const auto guests = simulation::import_guests(world, people);
    CHECK(simulation::start_guests(world) == 2);
    for (const entt::entity g : guests)
        CHECK(world.registry.get<simulation::Stride>(g).from == path);
}

TEST_CASE("saved litter, bins, benches, signs and staff come over onto the path tiles they stand on")
{
    simulation::SimWorld world;
    const auto tile = world.paths.add({ 2, 1, 0 }); // Middle at x 10, z 6
    const glm::vec3 middle = world.paths.centre(tile);

    save::model::PeopleModel people;
    people.litter.push_back({ .position = { middle.x + 1, 0, middle.z }, .type = 3, .create_time = 100 });
    people.litter.push_back({ .position = { middle.x, 0, middle.z }, .state = save::model::LitterState::Carried });
    people.litter.push_back({ .position = { 50, 0, 50 } }); // Off the paths
    CHECK(simulation::import_litter(world, people, 124) == 1);
    const auto pieces = world.registry.view<simulation::Litter>();
    REQUIRE(pieces.size() == 1);
    const auto& piece = world.registry.get<simulation::Litter>(pieces.front());
    CHECK(piece.node == tile);
    CHECK(piece.kind == 3);
    CHECK(piece.age.value == doctest::Approx(40)); // A game hour of the calendar

    save::model::WorldModel w;
    for (std::uint64_t id = 1; id <= 4; ++id)
        w.objects.push_back({ .id = id, .col = id == 4 ? 9 : 2, .row = 1 });
    w.bins.push_back({ .object_ref = 1, .capacity = 0.4f, .contents = 0.03f });
    w.benches = { 2, 4 }; // The second stands off the paths
    w.signs.push_back({ .object_ref = 3 });
    CHECK(simulation::import_path_items(world, w.objects, w.bins, w.benches, w.signs) == 3);
    const auto bins = world.registry.view<simulation::Bin>();
    REQUIRE(bins.size() == 1);
    CHECK(world.registry.get<simulation::Bin>(bins.front()).capacity == doctest::Approx(40));
    CHECK(world.registry.get<simulation::Bin>(bins.front()).contents == 3);
    for (const auto [_, item] : world.registry.view<simulation::PathItem>().each())
    {
        CHECK(item.node == tile);
        CHECK(item.object != 0); // So it goes when its object is taken away
    }
    save::model::PlacedObject gone { .id = 5, .col = 2, .row = 1 };
    gone.hidden = true;
    CHECK(simulation::add_path_item(world, gone, simulation::PathItemKind::Bench) == entt::null);

    people.staff.resize(4);
    people.staff[0].kind = save::model::StaffKind::Handyman;
    people.staff[0].person.position = { middle.x, 0, middle.z };
    people.staff[1].kind = save::model::StaffKind::SecurityGuard;
    people.staff[1].person.position = { middle.x, 0, middle.z };
    people.staff[2].person.position = { 50, 0, 50 };
    people.staff[3].kind = save::model::StaffKind::Vendor;
    people.staff[3].person.position = { 51, 0, 52 };
    const auto staff = simulation::import_staff(world, people);
    REQUIRE(staff.size() == 4);
    // A vendor stays in their booth, off the paths.
    CHECK(world.registry.get<simulation::Staff>(staff[3]).role == simulation::StaffRole::Vendor);
    CHECK_FALSE(world.registry.all_of<simulation::Stride>(staff[3]));
    CHECK(world.registry.get<simulation::Pose>(staff[3]).position == glm::vec3 { 51, 0, 52 });
    CHECK(world.registry.get<simulation::Staff>(staff[0]).role == simulation::StaffRole::Handyman);
    CHECK(world.registry.get<simulation::Staff>(staff[1]).role == simulation::StaffRole::Guard);
    CHECK(world.registry.get<simulation::Stride>(staff[0]).to == tile);
    CHECK(world.registry.get<simulation::Stride>(staff[2]).to == tile); // Off the paths: from the nearest walkway
}

TEST_CASE("a seller hired with a placed stall stands in their booth, off the paths, paid as new staff are")
{
    simulation::SimWorld world;
    const entt::entity seller = simulation::add_vendor(world, { 3, 1, 4 });
    const auto& staff = world.registry.get<simulation::Staff>(seller);
    CHECK(staff.role == simulation::StaffRole::Vendor);
    CHECK(staff.wage.value > 0);
    CHECK(world.registry.get<simulation::Pose>(seller).position == glm::vec3 { 3, 1, 4 });
    CHECK_FALSE(world.registry.all_of<simulation::Stride>(seller));
}

TEST_CASE("a saved lifeguard sits where the park left them, off the paths, unless their chair is gone")
{
    simulation::SimWorld world;
    (void)world.paths.add({ 2, 1, 0 });
    save::model::PeopleModel people;
    people.staff.resize(2);
    for (auto& s : people.staff)
    {
        s.kind = save::model::StaffKind::LifeGuard;
        s.person.position = { 30, 1, 40 };
    }
    people.staff[0].work = save::model::LifeGuardWork { .chair = { 7 }, .chair_been_set = true };
    people.staff[1].work = save::model::LifeGuardWork { };
    const auto staff = simulation::import_staff(world, people);
    REQUIRE(staff[0] != entt::null);
    CHECK(world.registry.get<simulation::Staff>(staff[0]).role == simulation::StaffRole::Other);
    CHECK_FALSE(world.registry.all_of<simulation::Stride>(staff[0]));
    CHECK(world.registry.get<simulation::Pose>(staff[0]).position == glm::vec3 { 30, 1, 40 });
    CHECK(staff[1] == entt::null);
}

TEST_CASE("a lifeguard come with a placed chair sits in it, unpaid")
{
    simulation::SimWorld world;
    const entt::entity guard = simulation::add_lifeguard(world, { 3, 1, 4 }, { 1.5f });
    const auto& staff = world.registry.get<simulation::Staff>(guard);
    CHECK(staff.role == simulation::StaffRole::Other);
    CHECK(staff.wage.value == 0);
    CHECK(world.registry.get<simulation::Pose>(guard).position == glm::vec3 { 3, 1, 4 });
    CHECK(world.registry.get<simulation::Pose>(guard).heading.value == 1.5f);
    CHECK_FALSE(world.registry.all_of<simulation::Stride>(guard));
}

TEST_CASE("paths laid and taken away come and go from the network; walkers and rides catch up")
{
    simulation::SimWorld world;
    save::model::TerrainGrid grid;
    grid.width = 8;
    grid.height = 4;
    grid.tile_size_x = 4;
    grid.tile_size_z = 4;
    grid.cells.resize(32);
    save::model::WorldModel model;
    model.terrain = grid;
    simulation::import_paths(world, model.paths, model.terrain);
    // A row of paths along row 0, and a ride whose entrance at (5, 1) has no path beside it yet.
    std::vector<save::model::PathTile> laid;
    for (int c = 0; c <= 3; ++c)
    {
        save::model::PathTile& p = laid.emplace_back();
        p.kind = save::model::PathKind::Ground;
        p.col = static_cast<std::uint8_t>(c);
        p.id = static_cast<std::uint64_t>(c + 1);
        CHECK(simulation::add_path(world, p, grid) != simulation::kNoNode);
    }
    save::model::RideModel rides;
    save::model::Ride& ride = rides.rides.emplace_back();
    ride.kind = save::model::RideKind::Flat;
    ride.base.id = 42;
    ride.stations.emplace_back().entrance_spot = save::model::PlacementSpot { .col = 5, .row = 1, .direction = 1 }; // Its back to (5, 0)
    const std::array<std::optional<simulation::RideTiming>, 1> timings { simulation::RideTiming { .seats = 4, .run = 30 } };
    const auto at = [&](int c) { return *world.paths.find({ c, 0, 0 }); };
    // Imported while a stub path reaches the entrance, then the stub goes and a path is laid to it later.
    save::model::PathTile stub;
    stub.kind = save::model::PathKind::Ground;
    stub.col = 5;
    (void)simulation::add_path(world, stub, grid);
    const auto e = simulation::import_rides(world, rides, timings).front();
    REQUIRE(world.registry.valid(e));
    simulation::remove_path(world, stub, grid);
    save::model::PathTile& joining = laid.emplace_back();
    joining.kind = save::model::PathKind::Ground;
    joining.col = 4;
    (void)simulation::add_path(world, joining, grid);
    save::model::PathTile& front = laid.emplace_back();
    front.kind = save::model::PathKind::Ground;
    front.col = 5;
    (void)simulation::add_path(world, front, grid);
    CHECK(simulation::relink_attractions(world, rides) == 1);
    CHECK(world.registry.get<simulation::Attraction>(e).entrance == at(5));
    CHECK(simulation::relink_attractions(world, rides) == 0);

    // A guest walking from (1, 0) to (2, 0) on the way to (3, 0); then (2, 0) is taken away.
    auto& r = world.registry;
    const entt::entity walker = r.create();
    r.emplace<simulation::Pose>(walker, simulation::Pose { .position = world.paths.centre(at(1)) });
    r.emplace<simulation::Stride>(walker, simulation::Stride { .from = at(1), .to = at(2), .progress = 0.5f, .route = { at(3) } });
    r.emplace<simulation::Errand>(walker, simulation::Errand { .kind = simulation::ErrandKind::Heading, .target = e });
    const simulation::NodeId middle = at(2);
    const entt::entity litter = r.create();
    r.emplace<simulation::Litter>(litter, simulation::Litter { .node = middle });
    simulation::remove_path(world, laid[2], grid);
    CHECK_FALSE(world.paths.find({ 2, 0, 0 }));
    const auto& stride = r.get<simulation::Stride>(walker);
    CHECK(stride.from == at(1));
    CHECK(stride.to == at(1));
    CHECK(stride.route.empty());
    CHECK(r.get<simulation::Errand>(walker).kind == simulation::ErrandKind::Wandering);
    CHECK_FALSE(r.valid(litter));
    CHECK_FALSE(world.paths.route(at(1), at(3)));
}

TEST_CASE("paths on the ground and the people on them follow the ground when it moves")
{
    simulation::SimWorld world;
    save::model::TerrainGrid grid;
    grid.width = 4;
    grid.height = 1;
    grid.tile_size_x = 4;
    grid.tile_size_z = 4;
    grid.cells.resize(4);
    save::model::WorldModel model;
    model.terrain = grid;
    simulation::import_paths(world, model.paths, model.terrain);
    std::vector<save::model::PathTile> laid;
    for (int c = 0; c <= 2; ++c)
    {
        save::model::PathTile& p = laid.emplace_back();
        p.kind = save::model::PathKind::Ground;
        p.col = static_cast<std::uint8_t>(c);
        (void)simulation::add_path(world, p, grid);
    }
    const auto at = [&](int c, int level) { return *world.paths.find({ c, 0, level }); };
    auto& r = world.registry;
    // One guest stands on (1, 0), another reels already, a third rides and is left be.
    const auto guest = [&](simulation::NodeId on) {
        const entt::entity g = r.create();
        r.emplace<simulation::Visitor>(g);
        r.emplace<simulation::Pose>(g, simulation::Pose { .position = world.paths.centre(on) });
        r.emplace<simulation::Stride>(g, simulation::Stride { .from = on, .to = on });
        return g;
    };
    const entt::entity standing = guest(at(1, 0));
    const entt::entity reeling = guest(at(1, 0));
    r.emplace<simulation::Gesture>(reeling, simulation::GestureKind::Dizzy, units::Seconds { 0.5f });
    const entt::entity riding = guest(at(1, 0));
    r.emplace<simulation::Thrill>(riding);
    // A handyman on the path, and a keeper on the bare ground of (3, 0).
    const entt::entity handyman = r.create();
    r.emplace<simulation::Staff>(handyman);
    r.emplace<simulation::Pose>(handyman, simulation::Pose { .position = world.paths.centre(at(1, 0)) });
    r.emplace<simulation::Stride>(handyman, simulation::Stride { .from = at(1, 0), .to = at(1, 0) });
    const entt::entity keeper = r.create();
    r.emplace<simulation::KeeperDuty>(keeper);
    r.emplace<simulation::Pose>(keeper, simulation::Pose { .position = { 14.0f, 0.0f, 2.0f } });

    // The ground under all four tiles rises two units.
    const save::model::TerrainGrid was = grid;
    for (auto& cell : grid.cells)
        cell.corners = { 2.0f, 2.0f, 2.0f, 2.0f };
    simulation::follow_ground(world, laid, was, grid, { 0, 0 }, { 3, 0 });
    CHECK_FALSE(world.paths.find({ 1, 0, 0 }));
    const simulation::NodeId raised = at(1, 2);
    CHECK(world.paths.nodes()[raised].height == doctest::Approx(2.0f));
    CHECK(world.paths.nodes()[raised].next[0] == at(2, 2));
    CHECK(world.paths.nodes()[raised].next[2] == at(0, 2));
    CHECK(r.get<simulation::Pose>(standing).position.y == doctest::Approx(2.0f));
    CHECK(r.get<simulation::Gesture>(standing).kind == simulation::GestureKind::Dizzy);
    CHECK(r.get<simulation::Gesture>(reeling).left.value == doctest::Approx(0.5f));
    CHECK(r.get<simulation::Pose>(riding).position.y == doctest::Approx(0.0f));
    CHECK_FALSE(r.all_of<simulation::Gesture>(riding));
    CHECK(r.get<simulation::Pose>(handyman).position.y == doctest::Approx(2.0f));
    CHECK_FALSE(r.all_of<simulation::Gesture>(handyman));
    CHECK(r.get<simulation::Pose>(keeper).position.y == doctest::Approx(2.0f));
}

TEST_CASE("people on foot whose ground the land tips too steep start sliding down it")
{
    simulation::SimWorld world;
    save::model::TerrainGrid grid;
    grid.width = 3;
    grid.height = 1;
    grid.tile_size_x = 4;
    grid.tile_size_z = 4;
    grid.cells.resize(3);
    save::model::WorldModel model;
    model.terrain = grid;
    simulation::import_paths(world, model.paths, model.terrain);
    std::vector<save::model::PathTile> laid;
    for (int c = 0; c <= 2; ++c)
    {
        save::model::PathTile& p = laid.emplace_back();
        p.kind = save::model::PathKind::Ground;
        p.col = static_cast<std::uint8_t>(c);
        (void)simulation::add_path(world, p, grid);
    }
    auto& r = world.registry;
    const auto person = [&](int col) {
        const simulation::NodeId on = *world.paths.find({ col, 0, 0 });
        const entt::entity g = r.create();
        r.emplace<simulation::Visitor>(g);
        r.emplace<simulation::Errand>(g);
        r.emplace<simulation::Pose>(g, simulation::Pose { .position = world.paths.centre(on) });
        r.emplace<simulation::Stride>(g, simulation::Stride { .from = on, .to = on });
        return g;
    };
    const entt::entity on_steep = person(1);
    const entt::entity on_gentle = person(2);
    const entt::entity seated = person(1);
    r.emplace<simulation::Linger>(seated);

    // Tile (1, 0) tips three units down along +x; (2, 0) half a unit.
    const save::model::TerrainGrid was = grid;
    grid.cells[1].corners = { 3.0f, 0.0f, 3.0f, 0.0f };
    grid.cells[2].corners = { 0.5f, 0.0f, 0.5f, 0.0f };
    simulation::follow_ground(world, laid, was, grid, { 0, 0 }, { 2, 0 });
    REQUIRE(r.all_of<simulation::Dropped>(on_steep));
    CHECK(r.get<simulation::Dropped>(on_steep).sliding);
    CHECK_FALSE(r.all_of<simulation::Dropped>(on_gentle));
    CHECK_FALSE(r.all_of<simulation::Dropped>(seated));
}

TEST_CASE("walkers stand on a ramp's slope and on the ground's own tilt under a path")
{
    simulation::SimWorld world;
    save::model::TerrainGrid grid;
    grid.width = 4;
    grid.height = 4;
    grid.tile_size_x = 4;
    grid.tile_size_z = 4;
    grid.cells.resize(16);
    grid.cells[1].corners = { 0.0f, 0.0f, 1.0f, 1.0f }; // Tile (1, 0) rises towards higher rows
    save::model::WorldModel model;
    model.terrain = grid;
    simulation::import_paths(world, model.paths, model.terrain);
    save::model::PathTile ground;
    ground.kind = save::model::PathKind::Ground;
    ground.col = 1;
    const auto bank = simulation::add_path(world, ground, grid);
    // A gentle ramp on tile (2, 1), from 2 at its low-column edge up to 3 at its high-column edge.
    save::model::PathTile ramp;
    ramp.kind = save::model::PathKind::Flying;
    ramp.col = 2;
    ramp.row = 1;
    ramp.direction = 1;
    ramp.elevation = save::model::PathElevation { .slope = 4, .quantised_height = 2 };
    const auto up = simulation::add_path(world, ramp, grid);
    REQUIRE(bank != simulation::kNoNode);
    REQUIRE(up != simulation::kNoNode);
    CHECK(world.paths.surface(bank, bank, { 6.0f, 1.0f }) == doctest::Approx(0.25f));
    CHECK(world.paths.surface(bank, bank, { 6.0f, 3.0f }) == doctest::Approx(0.75f));
    CHECK(world.paths.surface(up, up, { 9.0f, 6.0f }) == doctest::Approx(2.25f));
    CHECK(world.paths.surface(up, up, { 11.0f, 5.0f }) == doctest::Approx(2.75f));
}

TEST_CASE("a queue joins the paths beside it only at its open ends, not along its railings")
{
    simulation::SimWorld world;
    save::model::TerrainGrid grid;
    grid.width = 4;
    grid.height = 4;
    grid.tile_size_x = 4;
    grid.tile_size_z = 4;
    grid.cells.resize(16);
    save::model::WorldModel model;
    model.terrain = grid;
    simulation::import_paths(world, model.paths, model.terrain);
    const auto lay = [&](std::uint8_t col, std::uint8_t row) {
        save::model::PathTile p;
        p.kind = save::model::PathKind::Ground;
        p.col = col;
        p.row = row;
        return simulation::add_path(world, p, grid);
    };
    // A ground queue on (1, 1) running along the columns: its ends face (0, 1) and (2, 1).
    save::model::PathTile q;
    q.kind = save::model::PathKind::Queue;
    q.col = 1;
    q.row = 1;
    q.elevation = save::model::PathElevation { .slope = save::model::kSlopeOnGround };
    q.queue = save::model::PathQueueInfo { .start_direction = 0, .end_direction = 1 };
    const auto queue = simulation::add_path(world, q, grid);
    const auto front = lay(0, 1);
    const auto beside = lay(1, 0);
    const auto& next = world.paths.nodes()[queue].next;
    CHECK(next[2] == front);               // Across its open end
    CHECK(next[3] == simulation::kNoNode); // Across its railing
    CHECK(world.paths.nodes()[beside].next[1] == simulation::kNoNode);
}

TEST_CASE("rides whose stations stand side by side on one level are linked, others not")
{
    simulation::SimWorld world;
    auto& r = world.registry;
    const std::array<entt::entity, 4> rides { r.create(), r.create(), r.create(), r.create() };
    const save::model::TerrainGrid grid { .tile_size_x = 4, .tile_size_z = 4 };
    // A station two tiles long running along the rows, in column `col`, at height `up`.
    const auto station = [](float col, float up) {
        park::track::TrackCourse c;
        c.points = { { col * 4 + 2, up, 0 }, { col * 4 + 2, up, 8 }, { col * 4 + 2, up, 20 } };
        c.kinds = { park::track::CourseKind::Station, park::track::CourseKind::Plain, park::track::CourseKind::Plain };
        return std::optional(c);
    };
    const std::array courses { station(0, 0), station(1, 0), station(3, 0), station(1, 5) }; // Beside, apart, above
    simulation::link_stations(world, rides, courses, grid);
    REQUIRE(r.all_of<simulation::StationNeighbours>(rides[0]));
    CHECK(r.get<simulation::StationNeighbours>(rides[0]).rides == std::vector { rides[1] });
    CHECK(r.get<simulation::StationNeighbours>(rides[1]).rides == std::vector { rides[0] });
    CHECK_FALSE(r.all_of<simulation::StationNeighbours>(rides[2]));
    CHECK_FALSE(r.all_of<simulation::StationNeighbours>(rides[3]));
}

TEST_CASE("a no-entry sign bars the side its front faces; staff are kept off land not owned and the entrance")
{
    simulation::SimWorld world;
    save::model::TerrainGrid grid;
    grid.width = 5;
    grid.height = 1;
    grid.tile_size_x = 4;
    grid.tile_size_z = 4;
    grid.cells.resize(5);
    for (int c = 0; c < 4; ++c)
        grid.cells[static_cast<std::size_t>(c)].flags = static_cast<std::uint8_t>(save::model::TileFlag::Owned);
    for (int c = 0; c < 5; ++c)
    {
        save::model::PathTile p;
        p.kind = save::model::PathKind::Ground;
        p.col = static_cast<std::uint8_t>(c);
        (void)simulation::add_path(world, p, grid);
    }
    const auto at = [&](int c) { return *world.paths.find({ c, 0, 0 }); };
    CHECK(world.paths.nodes()[at(4)].staff_kept_off); // Not the park's
    CHECK_FALSE(world.paths.nodes()[at(3)].staff_kept_off);

    // An entrance two tiles across its own X, turned a quarter so they lie along the row: columns 2 and 3.
    const std::array<save::model::PlacedObject, 2> objects { save::model::PlacedObject { .id = 1, .col = 2, .row = 0, .rotation = 1 }, save::model::PlacedObject { .id = 2, .col = 1, .row = 0 } };
    REQUIRE(simulation::import_gate(world, objects, [](const save::model::PlacedObject& o) { return o.id == 1 ? std::optional(std::array<std::int32_t, 2> { 1, 2 }) : std::nullopt; }) != entt::null);
    simulation::keep_staff_in(world, grid);
    CHECK(world.paths.nodes()[at(2)].staff_kept_off);
    CHECK(world.paths.nodes()[at(3)].staff_kept_off);
    CHECK_FALSE(world.paths.nodes()[at(1)].staff_kept_off);

    // Unturned, a sign's front faces lower columns.
    const std::array<save::model::SignState, 1> signs { save::model::SignState { .object_ref = 2, .kind = 1 } };
    CHECK(simulation::import_path_items(world, objects, { }, { }, signs) == 1);
    CHECK(world.paths.nodes()[at(1)].no_entry == 1U << 2);
    CHECK_FALSE(world.paths.may_step(at(0), at(1), simulation::Walker::Guest));
    CHECK(world.paths.may_step(at(2), at(1), simulation::Walker::Guest));
}

TEST_CASE("the park's spawn points come over on the path tiles they stand on")
{
    simulation::SimWorld world;
    save::model::TerrainGrid grid;
    grid.width = 4;
    grid.height = 1;
    grid.tile_size_x = 4;
    grid.tile_size_z = 4;
    grid.cells.resize(4);
    for (int c = 0; c < 4; ++c)
    {
        save::model::PathTile p;
        p.kind = save::model::PathKind::Ground;
        p.col = static_cast<std::uint8_t>(c);
        (void)simulation::add_path(world, p, grid);
    }
    const std::array<save::model::PlacedObject, 3> objects { save::model::PlacedObject { .id = 1, .col = 0, .row = 0 }, save::model::PlacedObject { .id = 2, .symbol = "InjectionPoint", .col = 3, .row = 0, .hidden = true }, save::model::PlacedObject { .id = 3, .symbol = "InjectionPoint", .col = 3, .row = 5, .hidden = true } };
    REQUIRE(simulation::import_gate(world, objects, [](const save::model::PlacedObject& o) { return o.id == 1 ? std::optional(std::array<std::int32_t, 2> { 1, 1 }) : std::nullopt; }) != entt::null);
    const auto spawns = world.registry.view<const simulation::GuestSpawn>();
    REQUIRE(spawns.size() == 1); // The one off the paths is left out
    CHECK(spawns.get<const simulation::GuestSpawn>(spawns.front()).node == *world.paths.find({ 3, 0, 0 }));
}
