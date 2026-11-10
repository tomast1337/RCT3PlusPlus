// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/pools/pools.hpp"
#include "simulation/coaster.hpp"
#include "simulation/guests.hpp"
#include "simulation/import_park.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/swimming.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <limits>
#include <map>

using namespace rct3;
using namespace rct3::simulation;

namespace
{
    /// A path along row 0 from column 0 to 5, a changing room at (5, 1) opening on the paved square below it and
    /// three by two squares of water below that; and a slide for one rider at a time, from 2 m over the paved
    /// square down into the near row of water, its riders flying on clear of the far wall, its station first.
    entt::entity slide_by_pool(Sandbox& park)
    {
        park.path({ 0, 0 }, { 5, 0 });
        rct3::park::pools::PoolMap pools;
        pools.squares.push_back({ .col = 5, .row = 2, .kind = rct3::park::pools::PoolKind::Tile });
        for (std::int32_t row = 3; row < 5; ++row)
            for (std::int32_t col = 4; col < 7; ++col)
                pools.squares.push_back({ .col = col, .row = row });
        pools.rooms.push_back({ .col = 5, .row = 1, .side = rct3::park::Side { 0 }, .ticket = 200, .open = true });
        import_pools(park.world(), pools);
        auto& r = park.world().registry;
        const SwimPool& pool = r.get<SwimPool>(r.get<PoolDoor>(r.view<const PoolDoor>().front()).pool);
        const glm::vec3 top = pool.paved.front() + glm::vec3(0, 2, 0);
        const glm::vec3 bottom = *std::ranges::min_element(pool.water, { }, &glm::vec3::z) + glm::vec3(0, 0.5f, 0);
        const auto ride = park.flat_ride("Slide", { 4, 0 }, 100, { .capacity = 1 });
        Coaster train;
        train.points = { top, top + glm::normalize(bottom - top) * 2.0f, bottom };
        train.distance = { 0, 2, glm::distance(top, bottom) };
        for (const glm::vec3& p : train.points)
            train.height.push_back(p.y);
        train.kinds = { Stretch::Station, Stretch::Plain, Stretch::Plain };
        train.fixed_speed = 5;
        train.train_length = 1.5f;
        train.closed = false;
        train.splash_out = true;
        train.start = train.at = 2;
        r.emplace<Trains>(ride, Trains { { train }, { { } } });
        return ride;
    }

    bool thought(const entt::registry& r, entt::entity guest, ThoughtKind kind)
    {
        const auto* thoughts = r.try_get<Thoughts>(guest);
        return thoughts != nullptr && std::ranges::any_of(thoughts->recent, [&](const Thought& t) { return t.kind == kind; });
    }
} // namespace

TEST_CASE("a swimmer goes up a slide ending in their pool, rides it free in their swimwear and splashes down to swim on")
{
    Sandbox park;
    const auto slide = slide_by_pool(park);
    auto& r = park.world().registry;
    const auto guest = park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 });
    REQUIRE(park.run_until([&] { return r.get<Errand>(guest).kind == ErrandKind::Swimming && in_swimwear(r, guest); }, 120));
    const std::int64_t cash = r.get<Wallet>(guest).cash.value;
    // At the station they queue, and board the waiting car at once.
    REQUIRE(park.run_until([&] { return r.get<Errand>(guest).kind == ErrandKind::Riding; }, 600));
    CHECK(r.get<Errand>(guest).target == slide);
    CHECK(r.get<Trains>(slide).riders.front() == std::vector { guest });
    CHECK(in_swimwear(r, guest));
    CHECK(r.get<Wallet>(guest).cash.value == cash); // The complex's ticket covers it
    CHECK(r.get<Attraction>(slide).customers == 1);
    REQUIRE(park.run_until([&] { return r.all_of<Dropped>(guest); }, 60));
    CHECK(r.get<Dropped>(guest).slide == slide);
    CHECK(r.get<Dropped>(guest).drift.y > 0); // On down the slide's way
    // Its car flies on with them, onto the water.
    REQUIRE(r.get<Trains>(slide).trains.front().flight.has_value());
    const float water = r.get<Trains>(slide).trains.front().flight->water;
    CHECK(park.run_until(
        [&] {
            const auto& f = r.get<Trains>(slide).trains.front().flight;
            return f && f->afloat >= 0;
        },
        5));
    CHECK(r.get<Trains>(slide).trains.front().flight->at.y == doctest::Approx(water));
    CHECK(in_swimwear(r, guest));
    REQUIRE(park.run_until([&] { return r.any_of<Errand>(guest) && r.get<Errand>(guest).kind == ErrandKind::Swimming; }, 10));
    park.run(1);
    CHECK(in_swimwear(r, guest));
    CHECK(r.get<Swimmer>(guest).in_water);
    // No crash: nothing wrecked, nobody hurt, the slide still open, and its train back at the station.
    CHECK_FALSE(r.all_of<Wrecks>(slide));
    CHECK_FALSE(thought(r, guest, ThoughtKind::BeenInjured));
    CHECK(r.get<Attraction>(slide).open);
    const Coaster& train = r.get<Trains>(slide).trains.front();
    CHECK_FALSE(train.crashed);
    CHECK(park.run_until([&] { return !train.splashed && standing_at(train).has_value(); }, 10 + kGone));
}

TEST_CASE("a fast slide's car and rider thrown at a small pool's far wall stop at it and come down in the water")
{
    Sandbox park;
    const auto slide = slide_by_pool(park);
    auto& r = park.world().registry;
    r.get<Trains>(slide).trains.front().fixed_speed = 20; // Far past the far wall, two squares on
    const SwimPool& pool = r.get<SwimPool>(r.get<PoolDoor>(r.view<const PoolDoor>().front()).pool);
    const float wall = std::ranges::max(pool.water, { }, &glm::vec3::z).z + park.world().paths.tile_size / 2;
    const auto guest = park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 });
    REQUIRE(park.run_until([&] { return r.all_of<Dropped>(guest); }, 900));
    float furthest = 0;
    REQUIRE(park.run_until(
        [&] {
            if (const auto& f = r.get<Trains>(slide).trains.front().flight)
                furthest = std::max(furthest, f->at.z);
            furthest = std::max(furthest, r.get<Pose>(guest).position.z);
            return r.any_of<Errand>(guest) && r.get<Errand>(guest).kind == ErrandKind::Swimming;
        },
        10));
    CHECK(furthest > wall - park.world().paths.tile_size);
    CHECK(furthest < wall);
    CHECK(r.get<Swimmer>(guest).in_water);
}

TEST_CASE("a swimmer in a slide's queue swims on when the slide breaks down")
{
    Sandbox park;
    const auto slide = slide_by_pool(park);
    auto& r = park.world().registry;
    const auto guest = park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 });
    REQUIRE(park.run_until([&] { return r.get<Errand>(guest).kind == ErrandKind::Swimming && in_swimwear(r, guest); }, 120));
    Errand& errand = r.get<Errand>(guest);
    r.get<Swimmer>(guest).room = errand.target;
    errand = { .kind = ErrandKind::Queueing, .target = slide };
    r.get<CycleRide>(slide).queue.push_back(guest);
    r.get_or_emplace<Upkeep>(slide) = { .broken = true, .kind = Breakdown::EmergencyStop };
    park.run(1);
    CHECK(r.get<CycleRide>(slide).queue.empty());
    CHECK(r.get<Errand>(guest).kind == ErrandKind::Swimming);
    CHECK(in_swimwear(r, guest));
}

TEST_CASE("a pool slide's train kept past its open end is out of sight there, and the one at the top waits to go")
{
    Coaster slide;
    slide.points = { { 0, 2, 0 }, { 0, 0, 10 } };
    slide.distance = { 0, 10 };
    slide.kinds = { Stretch::Station, Stretch::Plain };
    slide.train_length = 1.5f;
    slide.closed = false;
    slide.splash_out = true;
    slide.start = slide.at = 2;
    save::model::Ride ride;
    ride.trains.resize(3);
    ride.trains[0].distance = 2;
    ride.trains[1].distance = 10;
    ride.trains[2].distance = 10;
    const Trains trains = trains_of(slide, ride);
    REQUIRE(trains.trains.size() == 3);
    CHECK(trains.trains[0].at == doctest::Approx(2));
    CHECK_FALSE(trains.trains[0].running);
    CHECK_FALSE(trains.trains[0].splashed);
    for (std::size_t t = 1; t < 3; ++t)
    {
        CHECK(trains.trains[t].splashed);
        CHECK_FALSE(trains.trains[t].running);
        CHECK_FALSE(trains.trains[t].crashed);
        CHECK(trains.trains[t].at == doctest::Approx(10));
    }
    // A coaster's train at the same place is only round its circuit again, at its start.
    slide.splash_out = false;
    slide.closed = true;
    CHECK_FALSE(trains_of(slide, ride).trains[1].splashed);
}

TEST_CASE("a mechanic mends a broken pool slide from its complex's changing room door, never going in")
{
    Sandbox park;
    const auto slide = slide_by_pool(park);
    auto& r = park.world().registry;
    auto& attraction = r.get<Attraction>(slide);
    attraction.entrance = r.get<CycleRide>(slide).exit = kNoNode; // A pool slide's platform has no doors of its own
    attraction.open = false;
    r.emplace_or_replace<Upkeep>(slide, Upkeep { .broken = true, .inspect_every = -1 });
    const auto mechanic = park.staff(StaffRole::Mechanic, { 0, 0 });
    const float pool_edge = park.world().paths.tile_size; // Row 0's far side; the complex starts past it
    bool kept_out = true;
    CHECK(park.run_until(
        [&] {
            kept_out = kept_out && r.get<Pose>(mechanic).position.z <= pool_edge;
            return r.get<Attraction>(slide).open;
        },
        300));
    CHECK(kept_out);
    CHECK(r.get<Staff>(mechanic).fixed == 1);
    CHECK(r.get<Pose>(mechanic).position.x >= 4 * park.world().paths.tile_size); // Mended at the room's door at column 5
}

TEST_CASE("swimmers queueing for a pool slide stand apart in a line, never two on one spot")
{
    Sandbox park;
    const auto slide = slide_by_pool(park);
    auto& r = park.world().registry;
    r.get<Attraction>(slide).entrance = r.get<CycleRide>(slide).exit = kNoNode; // Only swimmers queue, from the pool
    for (int i = 0; i < 20; ++i)
        park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 });
    // The longest line, and the closest two in it came once both had been in it a few seconds: two joining on one
    // spot, or lines from two ways meeting, step apart.
    std::size_t most = 0;
    float closest = std::numeric_limits<float>::max();
    std::map<entt::entity, int> joined;
    for (int t = 0; t < 6000; ++t)
    {
        park.run_seconds(0.1f);
        std::vector<glm::vec3> line;
        for (const entt::entity g : r.get<CycleRide>(slide).queue)
            if (t - joined.try_emplace(g, t).first->second >= 30)
                line.push_back(r.get<Pose>(g).position);
        std::erase_if(joined, [&](const auto& j) { return !std::ranges::contains(r.get<CycleRide>(slide).queue, j.first); });
        most = std::max(most, line.size());
        for (std::size_t i = 0; i < line.size(); ++i)
            for (std::size_t j = i + 1; j < line.size(); ++j)
                closest = std::min(closest, glm::distance(line[i], line[j]));
    }
    CAPTURE(most);
    CHECK(most >= 3);
    CHECK(closest > kTuning.pools.queue_gap / 2);
}
