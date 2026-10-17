// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/model/rides.hpp"
#include "simulation/components.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/import_park.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"

#include <doctest/doctest.h>

using namespace rct3::simulation;

TEST_CASE("a ride worn right down breaks now and then, and its queue gives up")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = park.flat_ride("Twister", { 4, 0 }, 0, { .capacity = 4, .cycle_seconds = 10 });
    for (int i = 0; i < 12; ++i)
        park.guest({ i % 4, 0 });
    auto& r = park.world().registry;
    r.emplace<Upkeep>(ride, Upkeep { .reliability = 0, .inspect_every = -1 });
    CHECK(park.run_until([&] { return r.get<Upkeep>(ride).broken; }, 3000));
    CHECK(r.get<Upkeep>(ride).breakdowns == 1);
    CHECK_FALSE(r.get<Attraction>(ride).open);
    park.run(1);
    CHECK(r.get<CycleRide>(ride).queue.empty());
    // Closed, it gets no riders and wears no further while it waits for a mechanic.
    park.run_seconds(300);
    CHECK(r.get<Upkeep>(ride).breakdowns == 1);
    CHECK(r.get<CycleRide>(ride).riders.empty());
}

TEST_CASE("the nearest free mechanic walks to a broken ride, repairs it and it opens again")
{
    Sandbox park;
    park.path({ 0, 0 }, { 6, 0 });
    const auto ride = park.flat_ride("Twister", { 6, 0 }, 0);
    auto& r = park.world().registry;
    r.emplace<Upkeep>(ride, Upkeep { .reliability = 0.5f, .broken = true, .inspect_every = -1 });
    r.get<Attraction>(ride).open = false;
    const auto near = park.staff(StaffRole::Mechanic, { 4, 0 });
    const auto far = park.staff(StaffRole::Mechanic, { 0, 0 });
    park.run(1);
    CHECK(r.get<Staff>(near).ride == ride);
    CHECK(r.get<Staff>(far).ride == entt::null);
    CHECK(park.run_until([&] { return r.get<Attraction>(ride).open; }, 120));
    CHECK_FALSE(r.get<Upkeep>(ride).broken);
    CHECK(r.get<Upkeep>(ride).reliability > 0.5f);
    CHECK(r.get<Staff>(near).ride == entt::null);
}

TEST_CASE("a mechanic inspects a ride when it is due, winning back reliability")
{
    Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    const auto ride = park.flat_ride("Twister", { 3, 0 }, 0);
    auto& r = park.world().registry;
    r.emplace<Upkeep>(ride, Upkeep { .reliability = 0.5f, .since_inspection = 0, .inspect_every = 60 });
    const auto mechanic = park.staff(StaffRole::Mechanic, { 0, 0 });
    park.run_seconds(50);
    CHECK(r.get<Staff>(mechanic).ride == entt::null); // Not due yet
    CHECK(park.run_until([&] { return r.get<Upkeep>(ride).reliability > 0.5f; }, 60));
    CHECK(r.get<Upkeep>(ride).since_inspection < 1);
}

TEST_CASE("staff are paid their wage by the game hour")
{
    Sandbox park;
    park.path({ 0, 0 }, { 2, 0 });
    for (int i = 0; i < 2; ++i)
        park.world().registry.get<Staff>(park.staff(StaffRole::Handyman, { 0, 0 })).wage = 100;
    park.run_seconds(kSecondsPerHour);
    CHECK(park.world().registry.ctx().get<Payroll>().paid == doctest::Approx(200).epsilon(0.02));
}

TEST_CASE("a saved ride brings its reliability, breakdown and inspections")
{
    rct3::save::Ride ride;
    ride.breakdown.reliability = 80;
    ride.breakdown.inspection_interval = 4; // Every hour
    ride.base.time_since_inspection = 90;
    Upkeep upkeep = upkeep_of(ride);
    CHECK(upkeep.reliability == doctest::Approx(0.8f));
    CHECK_FALSE(upkeep.broken);
    CHECK(upkeep.inspect_every == 3600);
    CHECK(upkeep.since_inspection == 90);
    ride.base.repair_status = rct3::save::RepairStatus::MechanicOnTheWay;
    ride.breakdown.inspection_interval = 6; // Never
    upkeep = upkeep_of(ride);
    CHECK(upkeep.broken);
    CHECK(upkeep.inspect_every < 0);
}
