// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/import_park.hpp"

#include "simulation/components.hpp"
#include "simulation/import_people.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3;

TEST_CASE("a show ride's go lasts as long as the tricks of its show")
{
    simulation::CycleRide ride { .start = { 3 }, .run = { 10 }, .stop = { 2 }, .loops = 4 };
    const save::RideShow show { .ride = 1, .tricks = { { .name = "a", .seconds = 5 }, { .name = "b", .seconds = 20 }, { .name = "c", .seconds = 7 } }, .order = { 1, 2, 1, 9 } }; // 9: no such trick
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
    save::RideModel rides;
    const auto stall = [&](std::string symbol, std::string item, int col, std::string kind = { }) {
        save::Stall& s = rides.stalls.emplace_back();
        s.base.symbol = std::move(symbol);
        s.base.spot = save::PlacementSpot { .col = col, .row = 1 };
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

TEST_CASE("a stall's food and drink is cold or hot by its name, and plain otherwise")
{
    simulation::SimWorld world;
    for (int c = 0; c <= 3; ++c)
        world.paths.add({ c, 0, 0 });
    save::RideModel rides;
    for (const auto& [item, col] : { std::pair { "Ice Cream", 0 }, std::pair { "Hot Chocolate", 1 }, std::pair { "Steak", 2 }, std::pair { "Milk Shake", 3 } })
    {
        save::Stall& s = rides.stalls.emplace_back();
        s.base.spot = save::PlacementSpot { .col = col, .row = 1 };
        s.items.push_back({ .price = 100, .name = item });
    }
    REQUIRE(simulation::import_stalls(world, rides) == 4);
    std::vector<simulation::Serve> served;
    for (const auto [_, shop] : world.registry.view<const simulation::Shop>().each())
        served.push_back(shop.serve);
    std::ranges::sort(served);
    CHECK(served == std::vector { simulation::Serve::Any, simulation::Serve::Cold, simulation::Serve::Cold, simulation::Serve::Hot });
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
    ride.base.symbol = "BumperBoats:ffr"; // Boats, which splash their riders
    ride.ticket_price = 150;
    ride.assessment = { .valid = true, .excitement = 5, .intensity = 2, .nausea = 12 };
    save::Station& station = ride.stations.emplace_back();
    station.entrance_spot = save::PlacementSpot { .col = 6, .row = 0 };
    station.exit_spot = save::PlacementSpot { .col = 4, .row = 1 };
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

TEST_CASE("boats for hire carry their riders out and back, though their dock is no circuit")
{
    simulation::SimWorld world;
    for (int c = 0; c <= 3; ++c)
        world.paths.add({ c, 0, 0 });
    save::RideModel rides;
    save::Ride& ride = rides.rides.emplace_back();
    ride.kind = save::RideKind::Tracked;
    ride.mode = save::OperatingMode::Boat;
    ride.seats.resize(8); // Four boats of two
    ride.stations.emplace_back().entrance_spot = save::PlacementSpot { .col = 4, .row = 0 };
    const auto added = simulation::import_rides(world, rides, { });
    REQUIRE(world.registry.valid(added[0]));
    const auto& cycle = world.registry.get<simulation::CycleRide>(added[0]);
    CHECK(cycle.capacity == 8);
    CHECK(cycle.run.value == simulation::kTuning.boats.trip.value);
    CHECK_FALSE(world.registry.all_of<simulation::Trains>(added[0]));
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
    const std::array<std::optional<simulation::RideTiming>, 1> timings { simulation::RideTiming { .seats = 2, .run = 40 } };
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
    save::PeopleModel people;
    for (const glm::vec3 at : { world.paths.centre(queue), glm::vec3(60, 0, 60) })
    {
        save::Guest& g = people.guests.emplace_back();
        g.in_park = true;
        g.person.id = static_cast<save::PeopleObjectId>(people.guests.size());
        g.person.state = save::PersonState::Swimming;
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

    save::PeopleModel people;
    people.litter.push_back({ .position = { middle.x + 1, 0, middle.z }, .type = 3, .create_time = 100 });
    people.litter.push_back({ .position = { middle.x, 0, middle.z }, .state = save::LitterState::Carried });
    people.litter.push_back({ .position = { 50, 0, 50 } }); // Off the paths
    CHECK(simulation::import_litter(world, people, 124) == 1);
    const auto pieces = world.registry.view<simulation::Litter>();
    REQUIRE(pieces.size() == 1);
    const auto& piece = world.registry.get<simulation::Litter>(pieces.front());
    CHECK(piece.node == tile);
    CHECK(piece.kind == 3);
    CHECK(piece.age.value == doctest::Approx(40)); // A game hour of the calendar

    save::WorldModel w;
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
        CHECK(item.node == tile);

    people.staff.resize(4);
    people.staff[0].kind = save::StaffKind::Handyman;
    people.staff[0].person.position = { middle.x, 0, middle.z };
    people.staff[1].kind = save::StaffKind::SecurityGuard;
    people.staff[1].person.position = { middle.x, 0, middle.z };
    people.staff[2].person.position = { 50, 0, 50 };
    people.staff[3].kind = save::StaffKind::Vendor;
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

TEST_CASE("paths laid and taken away come and go from the network; walkers and rides catch up")
{
    simulation::SimWorld world;
    save::TerrainGrid grid;
    grid.width = 8;
    grid.height = 4;
    grid.tile_size_x = 4;
    grid.tile_size_z = 4;
    grid.cells.resize(32);
    save::WorldModel model;
    model.terrain = grid;
    simulation::import_paths(world, model);
    // A row of paths along row 0, and a ride whose entrance at (5, 1) has no path beside it yet.
    std::vector<save::PathTile> laid;
    for (int c = 0; c <= 3; ++c)
    {
        save::PathTile& p = laid.emplace_back();
        p.kind = save::PathKind::Ground;
        p.col = static_cast<std::uint8_t>(c);
        p.id = static_cast<std::uint64_t>(c + 1);
        CHECK(simulation::add_path(world, p, grid) != simulation::kNoNode);
    }
    save::RideModel rides;
    save::Ride& ride = rides.rides.emplace_back();
    ride.kind = save::RideKind::Flat;
    ride.base.id = 42;
    ride.stations.emplace_back().entrance_spot = save::PlacementSpot { .col = 5, .row = 1 };
    const std::array<std::optional<simulation::RideTiming>, 1> timings { simulation::RideTiming { .seats = 4, .run = 30 } };
    const auto at = [&](int c) { return *world.paths.find({ c, 0, 0 }); };
    // Imported while a stub path reaches the entrance, then the stub goes and a path is laid to it later.
    save::PathTile stub;
    stub.kind = save::PathKind::Ground;
    stub.col = 5;
    stub.row = 2;
    (void)simulation::add_path(world, stub, grid);
    const auto e = simulation::import_rides(world, rides, timings).front();
    REQUIRE(world.registry.valid(e));
    simulation::remove_path(world, stub, grid);
    save::PathTile& joining = laid.emplace_back();
    joining.kind = save::PathKind::Ground;
    joining.col = 4;
    (void)simulation::add_path(world, joining, grid);
    save::PathTile& front = laid.emplace_back();
    front.kind = save::PathKind::Ground;
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
