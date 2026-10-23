// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/boat.hpp"

#include "simulation/components.hpp"
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
        docked = simulation::sail(boat, water, dock, 3, 40, 0.5f, dice);
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
        docked = simulation::sail(boat, water, dock, 3, 1e9f, 0.5f, dice);
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
