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
    // Closed, it takes no one on and wears no further while it waits for a mechanic.
    const std::uint32_t customers = r.get<Attraction>(ride).customers;
    park.run_seconds(300);
    CHECK(r.get<Upkeep>(ride).breakdowns == 1);
    CHECK(r.get<Attraction>(ride).customers == customers);
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
    CHECK(r.get<Upkeep>(ride).reliability.value > 0.5f);
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
    CHECK(park.run_until([&] { return r.get<Upkeep>(ride).reliability.value > 0.5f; }, 60));
    CHECK(r.get<Upkeep>(ride).since_inspection.value < 1);
}

TEST_CASE("a mechanic the player calls inspects the ride before it is due")
{
    Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    const auto ride = park.flat_ride("Twister", { 3, 0 }, 0);
    auto& r = park.world().registry;
    r.emplace<Upkeep>(ride, Upkeep { .reliability = 0.5f, .since_inspection = 0, .inspect_every = -1, .called = true });
    park.staff(StaffRole::Mechanic, { 0, 0 });
    CHECK(park.run_until([&] { return r.get<Upkeep>(ride).reliability.value > 0.5f; }, 60));
    CHECK_FALSE(r.get<Upkeep>(ride).called);
}

TEST_CASE("staff are paid their wage by the game hour")
{
    Sandbox park;
    park.path({ 0, 0 }, { 2, 0 });
    for (int i = 0; i < 2; ++i)
        park.world().registry.get<Staff>(park.staff(StaffRole::Handyman, { 0, 0 })).wage.value = 100;
    park.run_seconds(kSecondsPerHour);
    CHECK(park.world().registry.ctx().get<Payroll>().paid.value == doctest::Approx(200).epsilon(0.02));
}

TEST_CASE("a saved ride brings its reliability, breakdown and inspections")
{
    rct3::save::Ride ride;
    ride.breakdown.reliability = 80;
    ride.breakdown.inspection_interval = 4; // Every hour
    ride.base.time_since_inspection = 90;
    Upkeep upkeep = upkeep_of(ride);
    CHECK(upkeep.reliability.value == doctest::Approx(0.8f));
    CHECK_FALSE(upkeep.broken);
    CHECK(upkeep.inspect_every.value == 3600);
    CHECK(upkeep.since_inspection.value == 90);
    ride.base.repair_status = rct3::save::RepairStatus::MechanicCalled; // Kept on some fixed rides too
    CHECK_FALSE(upkeep_of(ride).broken);
    ride.base.flags |= rct3::save::AttractionCommon::kBrokenDown;
    ride.breakdown.inspection_interval = 6; // Never
    upkeep = upkeep_of(ride);
    CHECK(upkeep.broken);
    CHECK(upkeep.inspect_every.value < 0);
}

TEST_CASE("an emergency stop halts a coaster's train on the track till a mechanic fixes it")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = park.flat_ride("Looper", { 4, 0 }, 100, { .capacity = 2, .cycle_seconds = 1000 });
    auto& r = park.world().registry;
    Coaster train;
    train.distance = { 0, 50 };
    train.height = { 0, 0 };
    train.kinds = { Stretch::Plain, Stretch::Plain };
    train.fixed_speed = 5; // Round in 10 s
    r.emplace<Trains>(ride, Trains { { train }, { { } } });
    r.emplace<Upkeep>(ride, Upkeep { .inspect_every = -1 });
    const auto g = park.guest({ 4, 0 }, { .cash = 1000 });
    r.get<Errand>(g) = { ErrandKind::Heading, ride, 0 };
    park.run(2);
    REQUIRE(r.get<Trains>(ride).trains[0].running);
    auto& upkeep = r.get<Upkeep>(ride);
    upkeep.broken = true;
    upkeep.kind = upkeep.last = Breakdown::EmergencyStop;
    const float at = r.get<Trains>(ride).trains[0].travelled;
    park.run_seconds(30);
    CHECK(r.get<Trains>(ride).trains[0].travelled == at);
    CHECK(r.get<Errand>(g).kind == ErrandKind::Riding);
    park.staff(StaffRole::Mechanic, { 0, 0 });
    CHECK(park.run_until([&] { return r.get<Errand>(g).kind != ErrandKind::Riding; }, 60));
    CHECK(upkeep.kind == Breakdown::None);
    CHECK(upkeep.last == Breakdown::EmergencyStop);
}

TEST_CASE("with its restraints stuck shut a ride keeps its riders on till fixed")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = park.flat_ride("Twister", { 4, 0 }, 0, { .capacity = 2, .cycle_seconds = 10 });
    auto& r = park.world().registry;
    r.emplace<Upkeep>(ride, Upkeep { .inspect_every = -1 });
    const auto g = park.guest({ 4, 0 });
    r.get<Errand>(g) = { ErrandKind::Heading, ride, 0 };
    park.run(2);
    REQUIRE(r.get<Errand>(g).kind == ErrandKind::Riding);
    r.get<Upkeep>(ride).broken = true;
    r.get<Upkeep>(ride).kind = Breakdown::RestraintsFailToOpen;
    park.run_seconds(60);
    CHECK(r.get<Errand>(g).kind == ErrandKind::Riding);
    park.staff(StaffRole::Mechanic, { 0, 0 });
    CHECK(park.run_until([&] { return r.get<Errand>(g).kind != ErrandKind::Riding; }, 60));
}

TEST_CASE("a ride breaking down breaks in one of the ways its sort can, and a save keeps which")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = park.flat_ride("Twister", { 4, 0 }, 0, { .capacity = 4, .cycle_seconds = 10 });
    for (int i = 0; i < 8; ++i)
        park.guest({ i % 4, 0 });
    auto& r = park.world().registry;
    r.emplace<Upkeep>(ride, Upkeep { .reliability = 0, .inspect_every = -1 });
    CHECK(park.run_until([&] { return r.get<Upkeep>(ride).broken; }, 3000));
    CHECK(r.get<Upkeep>(ride).kind != Breakdown::None);
    CHECK(r.get<Upkeep>(ride).kind != Breakdown::EmergencyStop); // A coaster's, not a flat ride's
    rct3::save::Ride saved;
    saved.breakdown.current = rct3::save::BreakdownKind::DoorsFailToOpen;
    saved.base.flags = rct3::save::AttractionCommon::kBrokenDown;
    CHECK(upkeep_of(saved).kind == Breakdown::DoorsFailToOpen);
    saved.base.flags = 0; // A kind kept on a ride running fine
    CHECK_FALSE(upkeep_of(saved).broken);
    CHECK(upkeep_of(saved).kind == Breakdown::None);
}

TEST_CASE("a mechanic leaves their patrol for a breakdown, and goes back to it after")
{
    Sandbox park;
    park.path({ 0, 0 }, { 8, 0 });
    const auto ride = park.flat_ride("Twister", { 8, 0 }, 0, { .capacity = 2, .cycle_seconds = 10 });
    auto& r = park.world().registry;
    r.emplace<Upkeep>(ride, Upkeep { .broken = true, .inspect_every = -1 });
    const auto mechanic = park.staff(StaffRole::Mechanic, { 0, 0 });
    r.emplace<Patrol>(mechanic, Patrol { { { 0, 0 }, { 1, 0 }, { 2, 0 } } });
    CHECK(park.run_until([&] { return !r.get<Upkeep>(ride).broken; }, 120));
    const auto& nodes = park.world().paths.nodes();
    CHECK(park.run_until([&] { return nodes[r.get<Stride>(mechanic).from].tile.col <= 2; }, 120));
    park.run_seconds(300);
    CHECK(nodes[r.get<Stride>(mechanic).to].tile.col <= 2);
}

TEST_CASE("a ride's downtime counts the samples it was down in, window by window")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = park.flat_ride("Twister", { 4, 0 }, 0, { .capacity = 2, .cycle_seconds = 10 });
    auto& r = park.world().registry;
    r.emplace<Upkeep>(ride, Upkeep { .broken = true, .inspect_every = -1 });
    park.run_seconds(kSecondsPerHour + 1); // A whole window down, with no mechanic to fix it
    auto& upkeep = r.get<Upkeep>(ride);
    CHECK(upkeep.downtime[1] == 20);
    CHECK(upkeep.downtime_share() == doctest::Approx(20.0 / 160).epsilon(0.05));
    upkeep.broken = false;
    park.run_seconds(8 * kSecondsPerHour);
    CHECK(upkeep.downtime_share() == 0);
    rct3::save::Ride saved;
    saved.breakdown.downtime_windows = { 1, 2, 3, 4, 5, 6, 7, 8 };
    CHECK(upkeep_of(saved).downtime_share() == doctest::Approx(36.0 / 160));
}

TEST_CASE("a breakdown goes to the mechanic whose patrol takes the ride in, before a nearer one")
{
    Sandbox park;
    park.path({ 0, 0 }, { 8, 0 });
    const auto ride = park.flat_ride("Twister", { 8, 0 }, 0, { .capacity = 2, .cycle_seconds = 10 });
    auto& r = park.world().registry;
    const auto near = park.staff(StaffRole::Mechanic, { 6, 0 });
    const auto far = park.staff(StaffRole::Mechanic, { 0, 0 });
    r.emplace<Patrol>(near, Patrol { { { 5, 0 }, { 6, 0 } } });
    r.emplace<Patrol>(far, Patrol { { { 0, 0 }, { 8, 0 } } });
    r.emplace<Upkeep>(ride, Upkeep { .broken = true, .inspect_every = -1 });
    park.run(1);
    CHECK(r.get<Upkeep>(ride).mechanic == far);
}
