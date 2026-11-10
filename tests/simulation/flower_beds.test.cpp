// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/components.hpp"
#include "simulation/import_park.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <doctest/doctest.h>

using namespace rct3::simulation;

namespace
{
    /// A bed on the tile at `col`, `row`, holding `water` of 300.
    entt::entity bed(Sandbox& park, std::int32_t col, std::int32_t row, float water)
    {
        return add_flower_bed(park.world(), rct3::save::model::PlacedObject { .id = 7, .col = col, .row = row }, 300, water);
    }
} // namespace

TEST_CASE("a flower bed is watered from the path beside it, and starts full when never watered")
{
    Sandbox park;
    park.path({ 0, 0 }, { 5, 0 });
    const auto& paths = park.world().paths;
    auto& r = park.world().registry;
    const auto beside = bed(park, 3, 1, -1);
    REQUIRE(beside != entt::null);
    CHECK(paths.nodes()[r.get<FlowerBed>(beside).node].tile.col == 3);
    CHECK(r.get<FlowerBed>(beside).water == 300);
    CHECK(bed(park, 3, 5, 0) == entt::null); // No path near
}

TEST_CASE("a flower bed dries out over time")
{
    Sandbox park;
    park.path({ 0, 0 }, { 0, 0 });
    auto& r = park.world().registry;
    const auto b = bed(park, 0, 1, 300);
    park.run_seconds(kTuning.handymen.dry.value / 2);
    CHECK(r.get<FlowerBed>(b).water == doctest::Approx(150).epsilon(0.02));
    park.run_seconds(kTuning.handymen.dry.value);
    CHECK(r.get<FlowerBed>(b).water == 0);
}

TEST_CASE("a handyman walks to a dry bed, waters it full and counts it; one still wet is left")
{
    Sandbox park;
    park.path({ 0, 0 }, { 9, 0 });
    auto& r = park.world().registry;
    const auto dry = bed(park, 8, 1, 0);
    const auto wet = bed(park, 2, 1, 300);
    const auto handyman = park.staff(StaffRole::Handyman, { 0, 0 });
    bool watering = false;
    CHECK(park.run_until(
        [&] {
            watering = watering || r.get<Staff>(handyman).job == StaffJob::Water;
            return r.get<FlowerBed>(dry).water > 0;
        },
        60));
    CHECK(watering);
    CHECK(r.get<FlowerBed>(dry).water == doctest::Approx(300).epsilon(0.01));
    CHECK(r.get<Staff>(handyman).watered == 1);
    CHECK(r.get<FlowerBed>(wet).water < 300); // Drying, not yet dry: passed by
}

TEST_CASE("a handyman leaves dry beds alone without the task, and those outside their patrol")
{
    for (const bool patrolled : { false, true })
    {
        Sandbox park;
        park.path({ 0, 0 }, { 9, 0 });
        auto& r = park.world().registry;
        const auto dry = bed(park, 8, 1, 0);
        const auto handyman = park.staff(StaffRole::Handyman, { 0, 0 });
        if (patrolled)
            r.emplace<Patrol>(handyman, Patrol { { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } } });
        else
            r.get<Staff>(handyman).tasks = kEveryTask & ~kWaterGardensTask;
        park.run_seconds(120);
        CHECK(r.get<FlowerBed>(dry).water == 0);
        CHECK(r.get<Staff>(handyman).watered == 0);
    }
}

TEST_CASE("two handymen do not both go to the same dry bed")
{
    Sandbox park;
    park.path({ 0, 0 }, { 9, 0 });
    auto& r = park.world().registry;
    const auto dry = bed(park, 9, 1, 0);
    const auto a = park.staff(StaffRole::Handyman, { 0, 0 });
    const auto b = park.staff(StaffRole::Handyman, { 1, 0 });
    CHECK(park.run_until([&] { return r.get<FlowerBed>(dry).water > 0; }, 60));
    park.run_seconds(kTuning.handymen.water.value * 2);
    CHECK(r.get<Staff>(a).watered + r.get<Staff>(b).watered == 1);
}
