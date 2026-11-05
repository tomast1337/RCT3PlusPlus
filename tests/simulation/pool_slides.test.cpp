// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/pools.hpp"
#include "simulation/coaster.hpp"
#include "simulation/import_park.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <doctest/doctest.h>

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
        park::PoolMap pools;
        pools.squares.push_back({ .col = 5, .row = 2, .kind = park::PoolKind::Tile });
        for (std::int32_t row = 3; row < 5; ++row)
            for (std::int32_t col = 4; col < 7; ++col)
                pools.squares.push_back({ .col = col, .row = row });
        pools.rooms.push_back({ .col = 5, .row = 1, .turn = 0, .ticket = 200, .open = true });
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
    CHECK(park.run_until([&] { return !train.splashed && standing_at(train).has_value(); }, 10));
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
