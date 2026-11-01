// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/components.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3::simulation;

namespace
{
    bool found(const entt::registry& r, Finding finding, entt::entity at)
    {
        const auto* findings = r.ctx().find<Findings>();
        return findings != nullptr && std::ranges::contains(findings->found, std::pair { finding, at });
    }
} // namespace

TEST_CASE("an inspector rides the first open flat ride free, then goes back to their rounds")
{
    Sandbox park;
    park.path({ 0, 0 }, { 6, 0 });
    auto& r = park.world().registry;
    const auto closed = park.flat_ride("Closed", { 3, 0 }, 300);
    r.get<Attraction>(closed).open = false;
    const auto ride = park.flat_ride("Twister", { 6, 0 }, 300, { .cycle_seconds = 10 });
    const auto later = park.flat_ride("Later", { 5, 0 }, 300, { .cycle_seconds = 10 });
    const auto inspector = park.staff(StaffRole::Inspector, { 0, 0 });
    CHECK(park.run_until([&] { return r.get<Errand>(inspector).kind == ErrandKind::Riding; }, 120));
    CHECK(r.get<Errand>(inspector).target == ride);
    CHECK(r.get<Attraction>(ride).income.value == 0);
    CHECK(r.get<Attraction>(ride).customers == 0);
    CHECK(park.run_until([&] { return !r.all_of<Needs>(inspector); }, 120));
    CHECK(r.get<Errand>(inspector).kind == ErrandKind::Wandering);
    CHECK_FALSE(r.all_of<Wallet>(inspector));
    CHECK(r.get<Attraction>(later).customers == 0);
}

TEST_CASE("an inspector keeps to their tasks and comes back to an attraction only after a while")
{
    Sandbox park;
    park.path({ 0, 0 }, { 6, 0 });
    auto& r = park.world().registry;
    const auto ride = park.flat_ride("Twister", { 6, 0 }, 300, { .cycle_seconds = 10 });
    const auto stall = park.shop("Food", Product::Food, { 3, 0 }, 100);
    const auto inspector = park.staff(StaffRole::Inspector, { 0, 0 });
    r.get<Staff>(inspector).tasks = kFlatRidesTask;
    const auto visiting = [&](entt::entity at) { return r.all_of<Needs>(inspector) && r.get<Errand>(inspector).target == at; };
    CHECK(park.run_until([&] { return visiting(ride); }, 60));
    CHECK(park.run_until([&] { return !r.all_of<Needs>(inspector); }, 180));
    CHECK_FALSE(park.run_until([&] { return r.all_of<Needs>(inspector); }, kTuning.inspector.again.value * 0.8f));
    CHECK(park.run_until([&] { return visiting(ride); }, kTuning.inspector.again.value));
    CHECK_FALSE(found(r, Finding::TooCheap, stall)); // Never went to the stall
}

TEST_CASE("an inspector judges a stall's prices against what guests pay, then stands at its counter")
{
    struct Case
    {
        std::int64_t price;
        bool dear;
        bool cheap;
    };
    const std::int64_t worth = kTuning.eating.food_worth.value;
    for (const Case c : { Case { worth + 100, true, false }, Case { worth - 100, false, false }, Case { 10, false, true } })
    {
        CAPTURE(c.price);
        Sandbox park;
        park.path({ 0, 0 }, { 4, 0 });
        auto& r = park.world().registry;
        const auto stall = park.shop("Food", Product::Food, { 4, 0 }, c.price);
        r.get<Shop>(stall).menu = { { Product::Food, Serve::Any, { c.price }, "Burger" } };
        const auto inspector = park.staff(StaffRole::Inspector, { 0, 0 });
        r.get<Staff>(inspector).tasks = kRefreshmentsTask;
        CHECK(park.run_until([&] { return r.get<Errand>(inspector).kind == ErrandKind::Buying; }, 60));
        CHECK(found(r, Finding::TooDear, stall) == c.dear);
        CHECK(found(r, Finding::TooCheap, stall) == c.cheap);
        CHECK(r.get<Attraction>(stall).customers == 0);
        CHECK(park.run_until([&] { return !r.all_of<Needs>(inspector); }, 60));
    }
}

TEST_CASE("an inspector who queued for ages at a ride says so")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    auto& r = park.world().registry;
    const auto ride = park.flat_ride("Twister", { 4, 0 }, 300, { .cycle_seconds = kLongQueue + 30 });
    auto& cycle = r.get<CycleRide>(ride);
    cycle.phase = RidePhase::Running; // Busy with others till then
    const auto inspector = park.staff(StaffRole::Inspector, { 0, 0 });
    CHECK(park.run_until([&] { return r.get<Errand>(inspector).kind == ErrandKind::Queueing; }, 60));
    CHECK_FALSE(found(r, Finding::LongQueue, ride));
    CHECK(park.run_until([&] { return r.get<Errand>(inspector).kind == ErrandKind::Riding; }, kLongQueue + 60));
    CHECK(found(r, Finding::LongQueue, ride));
}
