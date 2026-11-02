// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/pools.hpp"
#include "simulation/components.hpp"
#include "simulation/import_park.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

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

    std::size_t times_found(const entt::registry& r, Finding finding)
    {
        const auto* findings = r.ctx().find<Findings>();
        return findings == nullptr ? 0 : static_cast<std::size_t>(std::ranges::count(findings->found, finding, &std::pair<Finding, entt::entity>::first));
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

TEST_CASE("an inspector never visits a kind of attraction the player switched off")
{
    Sandbox park;
    park.path({ 0, 0 }, { 6, 0 });
    auto& r = park.world().registry;
    const auto ride = park.flat_ride("Twister", { 3, 0 }, 300, { .cycle_seconds = 10 });
    const auto stall = park.shop("Food", Product::Food, { 6, 0 }, 100);
    const auto inspector = park.staff(StaffRole::Inspector, { 0, 0 });
    // Everything but the flat rides, the nearest attraction, as the staff window's switch leaves it.
    r.get<Staff>(inspector).tasks = ~kFlatRidesTask;
    const auto visiting = [&](entt::entity at) { return r.all_of<Needs>(inspector) && r.get<Errand>(inspector).target == at; };
    CHECK(park.run_until([&] { return visiting(stall); }, 60));
    CHECK_FALSE(park.run_until([&] { return visiting(ride); }, kTuning.inspector.again.value * 3));
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

TEST_CASE("an inspector judges a souvenir stall's prices against what its items are worth")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    auto& r = park.world().registry;
    const auto stall = park.shop("Umbrellas", Product::Umbrella, { 4, 0 }, 900);
    r.get<Shop>(stall).menu = { { .product = Product::Umbrella, .price = { 900 }, .worth = { Money { 600 }, Money { 600 }, Money { 600 } } } };
    const auto inspector = park.staff(StaffRole::Inspector, { 0, 0 });
    r.get<Staff>(inspector).tasks = kSouvenirsTask;
    CHECK(park.run_until([&] { return r.get<Errand>(inspector).kind == ErrandKind::Buying; }, 60));
    CHECK(found(r, Finding::TooDear, stall));
    CHECK(r.get<Attraction>(stall).customers == 0);
}

TEST_CASE("an inspector tells of a ride or stall unlike its scenery without going to it")
{
    Sandbox park;
    park.path({ 0, 0 }, { 6, 0 });
    auto& r = park.world().registry;
    const auto fits = park.shop("Fits", Product::Food, { 3, 0 }, 100);
    const auto odd = park.flat_ride("Odd", { 6, 0 }, 300);
    r.get<Attraction>(odd).off_theme = true;
    const auto inspector = park.staff(StaffRole::Inspector, { 0, 0 });
    r.get<Staff>(inspector).tasks = kVistasTask;
    CHECK(park.run_until([&] { return found(r, Finding::OffTheme, odd); }, 60));
    CHECK_FALSE(found(r, Finding::OffTheme, fits));
    CHECK_FALSE(r.all_of<Needs>(inspector));
}

TEST_CASE("an inspector goes after a worker, watches them and tells if they are lazy or unhappy")
{
    for (const bool good : { true, false })
    {
        CAPTURE(good);
        Sandbox park;
        park.path({ 0, 0 }, { 8, 0 });
        auto& r = park.world().registry;
        const auto worker = park.staff(StaffRole::Handyman, { 8, 0 });
        r.get<Staff>(worker).laziness = { good ? 0.0f : 0.9f };
        r.get<Staff>(worker).happiness = { good ? 1.0f : 0.1f };
        const auto inspector = park.staff(StaffRole::Inspector, { 0, 0 });
        r.get<Staff>(inspector).tasks = kStaffTask;
        const auto watching = [&] { return r.all_of<Rounds>(inspector) ? r.get<Rounds>(inspector).watching : entt::null; };
        CHECK(park.run_until([&] { return watching() == worker; }, 60));
        CHECK(park.run_until([&] { return watching() == entt::null; }, kTuning.inspector.chase.value + kTuning.inspector.watch.value));
        CHECK(found(r, Finding::Lazy, worker) == !good);
        CHECK(found(r, Finding::Unhappy, worker) == !good);
    }
}

TEST_CASE("an inspector swims free in a pool and tells how dirty it is")
{
    Sandbox park;
    park.path({ 0, 0 }, { 5, 0 });
    rct3::park::PoolMap pools;
    pools.squares.push_back({ .col = 5, .row = 2, .kind = rct3::park::PoolKind::Tile });
    for (std::int32_t row = 3; row < 5; ++row)
        for (std::int32_t col = 4; col < 7; ++col)
            pools.squares.push_back({ .col = col, .row = row });
    pools.rooms.push_back({ .col = 5, .row = 1, .turn = 0, .ticket = 200, .open = true });
    import_pools(park.world(), pools);
    auto& r = park.world().registry;
    const entt::entity room = r.view<const PoolDoor>().front();
    auto& pool = r.get<SwimPool>(r.get<PoolDoor>(room).pool);
    const auto inspector = park.staff(StaffRole::Inspector, { 0, 0 });
    r.get<Staff>(inspector).tasks = kPoolsTask;
    CHECK(park.run_until([&] { return r.all_of<Swimmer>(inspector); }, 60));
    CHECK(park.run_until(
        [&] {
            pool.dirt = { kTuning.inspector.filthy.value + 0.1f }; // Held where it is
            return !r.all_of<Needs>(inspector);
        },
        kTuning.pools.stay.value + 2 * kTuning.pools.changing.value + 120));
    CHECK(found(r, Finding::FilthyPool, room));
    CHECK(r.get<Attraction>(room).income.value == 0);
}

TEST_CASE("an inspector looking at the food and drink says what many of the guests want")
{
    Sandbox park;
    park.path({ 0, 0 }, { 6, 0 });
    auto& r = park.world().registry;
    for (int i = 0; i < 3; ++i)
        park.guest({ 6, 0 }, { .hunger = 0.9f });
    park.guest({ 6, 0 }, { .thirst = 0.9f });
    const auto inspector = park.staff(StaffRole::Inspector, { 0, 0 });
    r.get<Staff>(inspector).tasks = kRefreshmentsTask;
    CHECK(park.run_until([&] { return times_found(r, Finding::Hungry) > 0; }, 30));
    CHECK(found(r, Finding::Hungry, entt::null));
    CHECK(times_found(r, Finding::Thirsty) == 0);
}

TEST_CASE("an inspector tells of a broken ride with no mechanic near, and of an attraction off the paths")
{
    for (const bool mechanic : { false, true })
    {
        CAPTURE(mechanic);
        Sandbox park;
        park.path({ 0, 0 }, { 6, 0 });
        auto& r = park.world().registry;
        const auto ride = park.flat_ride("Broken", { 6, 0 }, 300);
        r.emplace_or_replace<Upkeep>(ride, Upkeep { .broken = true, .inspect_every = -1 });
        r.get<Attraction>(ride).open = false;
        const auto stall = park.shop("Lost", Product::Food, { 3, 0 }, 100);
        r.get<Attraction>(stall).entrance = kNoNode;
        if (mechanic)
            park.staff(StaffRole::Mechanic, { 6, 0 });
        const auto inspector = park.staff(StaffRole::Inspector, { 0, 0 });
        r.get<Staff>(inspector).tasks = kFlatRidesTask | kRefreshmentsTask;
        park.run_seconds(3 * kTuning.inspector.gap.value);
        CHECK(found(r, Finding::NoMechanic, ride) == !mechanic);
        CHECK(found(r, Finding::NoPath, stall));
    }
}

TEST_CASE("an inspector tells the same of the same attraction only once in a while")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    auto& r = park.world().registry;
    const auto stall = park.shop("Food", Product::Food, { 4, 0 }, kTuning.eating.food_worth.value + 100);
    const auto inspector = park.staff(StaffRole::Inspector, { 0, 0 });
    r.get<Staff>(inspector).tasks = kRefreshmentsTask;
    park.run_seconds(kTuning.inspector.again.value * 2.5f);
    CHECK(times_found(r, Finding::TooDear) == 1);
    park.run_seconds(kTuning.inspector.quiet.value);
    CHECK(times_found(r, Finding::TooDear) == 2);
    CHECK(found(r, Finding::TooDear, stall));
}
