// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/boat.hpp"

#include "park/path_clearance.hpp"
#include "simulation/components.hpp"
#include "simulation/import_park.hpp"
#include "simulation/sandbox.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3;

namespace
{
    /// A made-up lake: water everywhere in a `cols` by `rows` grid of 4 m tiles but on the tiles `land` lists.
    simulation::WaterTiles lake(int cols, int rows, std::initializer_list<glm::ivec2> land = { })
    {
        simulation::WaterTiles w { .cols = cols, .rows = rows };
        w.level.assign(static_cast<std::size_t>(cols * rows), 10.0f);
        for (const glm::ivec2 t : land)
            w.level[static_cast<std::size_t>(t.y * cols + t.x)] = std::nullopt;
        return w;
    }
} // namespace

TEST_CASE("only tiles wholly under water float a boat")
{
    save::TerrainGrid grid { .width = 3, .height = 1, .tile_size_x = 4, .tile_size_z = 4 };
    const save::WaterBody body { .level = 5, .tris = { { 0, 0, 0, 0b111 }, { 0, 0, 1, 0b111 }, { 1, 0, 0, 0b111 }, { 1, 0, 1, 0b011 } } };
    const auto water = simulation::water_tiles(grid, std::span { &body, 1 });
    CHECK(water.at(0, 0) == 5.0f);
    CHECK_FALSE(water.at(1, 0)); // One corner out of the water
    CHECK_FALSE(water.at(2, 0));
}

TEST_CASE("a rock in the water or a low bridge over it closes its tile to boats, a high one or a fence does not")
{
    save::TerrainGrid grid { .width = 5, .height = 1, .tile_size_x = 4, .tile_size_z = 4 };
    save::WaterBody body { .level = 5 };
    for (std::uint8_t col = 0; col < 5; ++col)
        body.tris.insert(body.tris.end(), { { col, 0, 0, 0b111 }, { col, 0, 1, 0b111 } });
    park::SolidTiles solids;
    solids.tiles[0] = { { .low = 2, .high = 6 } };                                                 // A rock standing up out of it
    solids.tiles[1] = { { .low = 5.5f, .high = 6 } };                                              // A low bridge
    solids.tiles[2] = { { .low = 9, .high = 10 } };                                                // A high one
    solids.tiles[3] = { { .low = 2, .high = 6, .room = park::edge_room(park::ModelTurn { 0 }) } }; // A fence along its edge
    const auto water = simulation::water_tiles(grid, std::span { &body, 1 }, solids);
    CHECK_FALSE(water.at(0, 0));
    CHECK_FALSE(water.at(1, 0));
    CHECK(water.at(2, 0));
    CHECK(water.at(3, 0));
    CHECK(water.at(4, 0));
}

TEST_CASE("a path bridge low over the water closes its tiles to boats, a high one leaves them open")
{
    save::TerrainGrid grid { .width = 3, .height = 1, .tile_size_x = 4, .tile_size_z = 4 };
    grid.cells.resize(3);
    save::WaterBody body { .level = 2 };
    for (std::uint8_t col = 0; col < 3; ++col)
        body.tris.insert(body.tris.end(), { { col, 0, 0, 0b111 }, { col, 0, 1, 0b111 } });
    const auto bridge = [](std::uint8_t col, std::int32_t height) { return save::PathTile { .kind = save::PathKind::Flying, .col = col, .row = 0, .elevation = save::PathElevation { .quantised_height = height } }; };
    const std::array paths { bridge(0, 2), bridge(1, 6) };
    park::SolidTiles solids;
    park::add_path_decks(solids, paths, grid);
    const auto water = simulation::water_tiles(grid, std::span { &body, 1 }, solids);
    CHECK_FALSE(water.at(0, 0)); // Its deck on the water
    CHECK(water.at(1, 0));       // Four metres over it
    CHECK(water.at(2, 0));
}

TEST_CASE("a boat's way goes round land, never cutting a corner past it")
{
    // A wall down the middle with a gap at the bottom.
    const auto water = lake(3, 3, { { 1, 0 }, { 1, 1 } });
    const auto way = simulation::water_route(water, { 0, 0 }, { 2, 0 });
    REQUIRE(way);
    CHECK(*way == std::vector<glm::ivec2> { { 0, 1 }, { 0, 2 }, { 1, 2 }, { 2, 2 }, { 2, 1 }, { 2, 0 } });
    CHECK_FALSE(simulation::water_route(lake(3, 1, { { 1, 0 } }), { 0, 0 }, { 2, 0 }));
}

TEST_CASE("a boat roams the lake, keeps to the water and docks once its time is up")
{
    const auto water = lake(12, 12, { { 5, 5 }, { 5, 6 }, { 6, 5 }, { 6, 6 } }); // An island
    const simulation::Dock dock { .leave = { 2, 2 }, .heading = { 1, 1 }, .enter = { 2, 6 } };
    simulation::Boat boat;
    simulation::cast_off(boat, dock);
    auto dice = random::seeded(random::Seed { 7 });
    bool docked = false;
    float seconds = 0;
    for (; seconds < 300 && !docked; seconds += 0.5f)
    {
        docked = simulation::sail(boat, water, dock, simulation::kMostBoats, 3, 40, 0.5f, dice);
        const glm::ivec2 t = water.tile_of(boat.at);
        // Turns are wide, so it may stray half a tile past the middles it steers by.
        const bool near_water = water.at(t.x, t.y) || water.at(water.tile_of(boat.at + glm::vec2 { 2, 0 }).x, t.y) || water.at(water.tile_of(boat.at - glm::vec2 { 2, 0 }).x, t.y) || water.at(t.x, water.tile_of(boat.at + glm::vec2 { 0, 2 }).y) || water.at(t.x, water.tile_of(boat.at - glm::vec2 { 0, 2 }).y);
        REQUIRE(near_water);
        CHECK(boat.speed <= 3.01f);
    }
    CHECK(docked);
    CHECK(seconds > 40);
    CHECK(seconds < 120);
    CHECK(boat.state == simulation::Sailing::Docked);
    CHECK(boat.at == dock.enter);
}

TEST_CASE("a boat ride lets its riders off only once its boats are back at the dock")
{
    simulation::Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    const auto ride = park.flat_ride("Rowing boats", { 3, 0 }, 0, { .capacity = 2, .cycle_seconds = 20 });
    auto& r = park.world().registry;
    r.ctx().emplace<simulation::WaterTiles>(lake(12, 12));
    const simulation::Dock dock { .leave = { 30, 2 }, .heading = { 0, 1 }, .enter = { 26, 2 } };
    r.emplace<simulation::Boats>(ride, simulation::Boats { .boats = { simulation::Boat { .at = dock.enter } }, .dock = dock });
    park.guest({ 0, 0 });
    const auto& boat = r.get<simulation::Boats>(ride).boats.front();
    REQUIRE(park.run_until([&] { return boat.state != simulation::Sailing::Docked; }, 600));
    const auto& riders = r.get<simulation::CycleRide>(ride).riders;
    CHECK(riders.size() == 1);
    CHECK(park.run_until([&] { return riders.empty(); }, 3000));
    CHECK(boat.state == simulation::Sailing::Docked);
}

TEST_CASE("a transport boat makes straight for each dock in turn and home, without roaming")
{
    const auto water = lake(12, 12, { { 5, 5 }, { 5, 6 }, { 6, 5 }, { 6, 6 } });
    const simulation::Dock dock { .leave = { 6, 2 }, .heading = { 1, 0 }, .enter = { 2, 2 } };
    const std::array<glm::vec2, 2> calls { glm::vec2 { 42, 42 }, glm::vec2 { 42, 6 } };
    simulation::Boat boat;
    simulation::cast_off(boat, dock, false, calls);
    CHECK(boat.state == simulation::Sailing::Home);
    auto dice = random::seeded(random::Seed { 3 });
    std::array<float, 2> nearest { 1e9f, 1e9f };
    bool docked = false;
    float seconds = 0;
    for (; seconds < 600 && !docked; seconds += 0.5f)
    {
        docked = simulation::sail(boat, water, dock, simulation::kMostBoats, 3, 1e9f, 0.5f, dice);
        for (std::size_t k = 0; k < calls.size(); ++k)
            nearest[k] = std::min(nearest[k], glm::distance(boat.at, calls[k]));
        // Round the island, not over it, though a wide turn may graze a corner.
        CHECK(glm::distance(boat.at, glm::vec2 { 24, 24 }) > 4);
    }
    CHECK(docked);
    CHECK(nearest[0] < 3);
    CHECK(nearest[1] < 3);
    CHECK(seconds < 120); // Some 110 m of water at up to 3 m/s
}

TEST_CASE("a boat slows in a hard turn and gains speed again once its mark lies ahead")
{
    const auto water = lake(20, 20);
    const simulation::Dock dock { .leave = { 40, 40 }, .heading = { 1, 0 }, .enter = { 40, 40 } };
    simulation::Boat boat;
    simulation::cast_off(boat, dock);
    boat.speed = 3;
    boat.marks = { { 40, 70 } }; // Off its side, far
    auto dice = random::seeded(random::Seed { 1 });
    simulation::sail(boat, water, dock, simulation::kMostBoats, 3, 1e9f, 0.5f, dice);
    CHECK(boat.speed < 2.5f);
    CHECK(boat.turning == doctest::Approx(simulation::kMostBoats.turn_most));
    for (int i = 0; i < 6; ++i)
        simulation::sail(boat, water, dock, simulation::kMostBoats, 3, 1e9f, 0.5f, dice);
    const float lined_up = boat.speed;
    for (int i = 0; i < 6; ++i)
        simulation::sail(boat, water, dock, simulation::kMostBoats, 3, 1e9f, 0.5f, dice);
    CHECK(boat.speed > lined_up);
}

TEST_CASE("a boat leans into its turn, no further than its handling lets it, and upright again on a straight")
{
    const auto water = lake(20, 20);
    const simulation::Dock dock { .leave = { 40, 40 }, .heading = { 1, 0 }, .enter = { 40, 40 } };
    const ovl::BoatHandling ski { .turn_most = 2, .turn_gain = 3, .speed_up = 2, .speed_gain = 0.5f, .turn_slow = 30, .brake = 5, .lean_gain = 0.9f, .lean_most = 0.3f };
    simulation::Boat boat;
    simulation::cast_off(boat, dock);
    boat.marks = { { 40, 70 } };
    auto dice = random::seeded(random::Seed { 1 });
    simulation::sail(boat, water, dock, ski, 3, 1e9f, 0.25f, dice);
    CHECK(boat.turning > 0);
    CHECK(boat.lean > 0);
    for (int i = 0; i < 8; ++i)
        simulation::sail(boat, water, dock, ski, 3, 1e9f, 0.25f, dice);
    CHECK(boat.lean <= ski.lean_most);
    boat.marks = { boat.at + boat.heading * 40.0f };
    for (int i = 0; i < 12; ++i)
        simulation::sail(boat, water, dock, ski, 3, 1e9f, 0.25f, dice);
    CHECK(std::abs(boat.lean) < 0.05f);
}

TEST_CASE("a boat is drawn between where its last sail began and ended, turning smoothly")
{
    simulation::Boat boat { .at = { 2, 0 }, .heading = { 0, 1 }, .lean = 0.2f, .was = { 0, 0 }, .was_heading = { 1, 0 }, .was_lean = 0 };
    const auto half = simulation::between_sails(boat, 0.5f);
    CHECK(half.at.x == doctest::Approx(1));
    CHECK(half.heading.x == doctest::Approx(std::sqrt(0.5f)));
    CHECK(half.heading.y == doctest::Approx(std::sqrt(0.5f)));
    CHECK(half.lean == doctest::Approx(0.1f));
    CHECK(simulation::between_sails(boat, 1).at == boat.at);
}

TEST_CASE("two boats heading at each other steer round and never touch")
{
    const auto water = lake(20, 20);
    const simulation::Dock dock { .leave = { 40, 40 }, .heading = { 1, 0 }, .enter = { 40, 40 } };
    std::array<simulation::Boat, 2> fleet;
    simulation::cast_off(fleet[0], { .leave = { 20, 40 }, .heading = { 1, 0 }, .enter = { 20, 40 } });
    simulation::cast_off(fleet[1], { .leave = { 60, 40.5f }, .heading = { -1, 0 }, .enter = { 60, 40 } });
    fleet[0].marks = { { 70, 40 } };
    fleet[1].marks = { { 10, 40 } };
    auto dice = random::seeded(random::Seed { 1 });
    float closest = 1e9f;
    for (int i = 0; i < 160; ++i)
    {
        for (auto& boat : fleet)
            simulation::sail(boat, water, dock, simulation::kMostBoats, 2, 1e9f, 0.25f, dice, fleet);
        closest = std::min(closest, glm::distance(fleet[0].at, fleet[1].at));
    }
    CHECK(fleet[0].at.x > 50); // Past each other
    CHECK(fleet[1].at.x < 30);
    CHECK(closest > 1.8f); // Never closer than their hulls touch at
}

TEST_CASE("bumper boats run into each other and bounce off")
{
    const auto water = lake(20, 20);
    const simulation::Dock dock { .leave = { 40, 40 }, .heading = { 1, 0 }, .enter = { 40, 40 } };
    std::array<simulation::Boat, 2> fleet;
    simulation::cast_off(fleet[0], { .leave = { 36, 40 }, .heading = { 1, 0 }, .enter = { 36, 40 } });
    simulation::cast_off(fleet[1], { .leave = { 44, 40 }, .heading = { -1, 0 }, .enter = { 44, 40 } });
    fleet[0].marks = { { 70, 40 } };
    fleet[1].marks = { { 10, 40 } };
    auto dice = random::seeded(random::Seed { 1 });
    for (int i = 0; i < 16; ++i)
        for (auto& boat : fleet)
            simulation::sail(boat, water, dock, simulation::kMostBoats, 2, 1e9f, 0.25f, dice, fleet, true);
    CHECK(fleet[0].at.x < 40); // Thrown back the way it came
    CHECK(fleet[1].at.x > 40);
    CHECK(glm::distance(fleet[0].at, fleet[1].at) >= 1.9f);
}

TEST_CASE("a rowing boat rows with both oars on a straight and with those that turn it in a turn")
{
    CHECK(simulation::stroke(0) == simulation::Stroke::Both);
    CHECK(simulation::stroke(simulation::kRowRound / 2) == simulation::Stroke::Both);
    CHECK(simulation::stroke(-simulation::kRowRound / 2) == simulation::Stroke::Both);
    CHECK(simulation::stroke(1) == simulation::Stroke::Left);
    CHECK(simulation::stroke(-1) == simulation::Stroke::Right);
}

TEST_CASE("a boat ride given boats again has them all back at its dock, as many as its trains, on the water as it is now")
{
    simulation::SimWorld world;
    auto& r = world.registry;
    const entt::entity ride = r.create();
    save::RideModel rides;
    rides.rides.push_back({ .mode = save::OperatingMode::Boat, .trains = { save::Train { }, save::Train { } } });
    // A dock of one 4 m station piece on the second tile of a lake three tiles long.
    const park::TrackCourse course { .points = { { 4, 5, 2 }, { 8, 5, 2 } }, .distance = { 0, 4 }, .kinds = { park::CourseKind::Station, park::CourseKind::Station } };
    const save::TerrainGrid grid { .width = 3, .height = 1, .tile_size_x = 4, .tile_size_z = 4 };
    save::WaterBody body { .level = 5 };
    for (std::uint8_t col = 0; col < 2; ++col)
        body.tris.insert(body.tris.end(), { { col, 0, 0, 0b111 }, { col, 0, 1, 0b111 } });
    const std::optional<park::TrackCourse> courses[] { course };
    simulation::import_boats(world, rides, std::span(&ride, 1), courses, grid, std::span(&body, 1));
    REQUIRE(r.all_of<simulation::Boats>(ride));
    CHECK(r.get<simulation::Boats>(ride).boats.size() == 2);
    CHECK_FALSE(r.ctx().get<simulation::WaterTiles>().at(2, 0));

    // Out on the water, its boats handling their own way; then a third boat, and the lake dug a tile longer.
    r.get<simulation::Boats>(ride).boats.front().at = { 6, 2 };
    r.get<simulation::Boats>(ride).handling.turn_most = 3;
    rides.rides.front().trains.emplace_back();
    body.tris.insert(body.tris.end(), { { 2, 0, 0, 0b111 }, { 2, 0, 1, 0b111 } });
    simulation::import_boats(world, rides, std::span(&ride, 1), courses, grid, std::span(&body, 1));
    const auto& boats = r.get<simulation::Boats>(ride);
    CHECK(boats.boats.size() == 3);
    CHECK(std::ranges::all_of(boats.boats, [&](const simulation::Boat& b) { return b.at == boats.dock.enter; }));
    CHECK(boats.handling.turn_most == 3);
    CHECK(r.ctx().get<simulation::WaterTiles>().at(2, 0));
}
