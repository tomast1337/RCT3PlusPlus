// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/pools.hpp"
#include "save/model/rides.hpp"
#include "simulation/coaster.hpp"
#include "simulation/import_park.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <array>
#include <doctest/doctest.h>
#include <numbers>
#include <optional>
#include <tuple>

using namespace rct3;
using namespace rct3::simulation;

namespace
{
    /// A path along row 0 from column 0 to 5, a changing room at (5, 1) opening on the paved square below it, three
    /// by two squares of water below that and a square `beside` at (7, 3) next to the water, pool path stairs climbing
    /// from the water unless told, their top at `surface`; and an open lazy river round a loop from column 8 to 12 and row 2 to 5, its station along
    /// column 8.
    entt::entity river_by_pool(Sandbox& park, park::PoolKind beside = park::PoolKind::PathStairs, std::int32_t surface = 0)
    {
        park.path({ 0, 0 }, { 5, 0 });
        park::PoolMap pools;
        pools.squares.push_back({ .col = 5, .row = 2, .kind = park::PoolKind::Tile });
        for (std::int32_t row = 3; row < 5; ++row)
            for (std::int32_t col = 4; col < 7; ++col)
                pools.squares.push_back({ .col = col, .row = row });
        pools.squares.push_back({ .col = 7, .row = 3, .kind = beside, .surface = surface - (beside == park::PoolKind::PathStairs ? park::kPathStairsRise : 0), .side = rct3::park::Side { 3 } });
        pools.rooms.push_back({ .col = 5, .row = 1, .side = rct3::park::Side { 0 }, .ticket = 200, .open = true });
        import_pools(park.world(), pools);
        const auto& paths = park.world().paths;
        const auto middle = [&](float col, float row) { return glm::vec3 { paths.origin.x + (col + 0.5f) * paths.tile_size, 0, paths.origin.y + (row + 0.5f) * paths.tile_size }; };
        Coaster course;
        for (const auto [col, row, kind] : std::array<std::tuple<float, float, Stretch>, 5> { { { 8.0f, 2.0f, Stretch::Station }, { 8.0f, 5.0f, Stretch::Plain }, { 12.0f, 5.0f, Stretch::Plain }, { 12.0f, 2.0f, Stretch::Plain }, { 8.0f, 2.0f, Stretch::Plain } } })
        {
            const glm::vec3 p = middle(col, row);
            course.distance.push_back(course.points.empty() ? 0 : course.distance.back() + glm::distance(course.points.back(), p));
            course.points.push_back(p);
            course.height.push_back(p.y);
            course.kinds.push_back(kind);
        }
        save::RideModel rides;
        save::Ride& ride = rides.rides.emplace_back();
        ride.mode = save::OperatingMode::LazyRiver;
        ride.base.state = save::AttractionState::Open;
        ride.ticket_price = 100;
        const std::array coasters { std::optional(course) };
        return import_rides(park.world(), rides, { }, coasters).front();
    }
} // namespace

TEST_CASE("a swimmer floats round a lazy river joined to their pool, free, and gets off up its stairs to swim on")
{
    Sandbox park;
    const auto river = river_by_pool(park);
    REQUIRE(river != entt::null);
    auto& r = park.world().registry;
    CHECK(r.get<Attraction>(river).price.value == 0);
    CHECK(r.get<CycleRide>(river).excitement.value > 0);
    const auto guest = park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 });
    REQUIRE(park.run_until([&] { return r.get<Errand>(guest).kind == ErrandKind::Swimming && in_swimwear(r, guest); }, 120));
    const std::int64_t cash = r.get<Wallet>(guest).cash.value;
    REQUIRE(park.run_until([&] { return r.all_of<Floating>(guest); }, 900));
    CHECK(r.get<River>(river).stations.front().pool == r.get<PoolDoor>(r.get<Swimmer>(guest).room).pool);
    CHECK(r.get<Errand>(guest).kind == ErrandKind::Riding);
    CHECK(r.get<Errand>(guest).target == river);
    CHECK(in_swimwear(r, guest));
    CHECK(r.get<Wallet>(guest).cash.value == cash); // The complex's ticket covers it
    CHECK(r.get<Attraction>(river).customers == 1);
    CHECK(r.get<CycleRide>(river).riders == std::vector { guest });
    // Along the course, carried round by the current.
    const float along = r.get<Floating>(guest).along;
    const glm::vec3 was = r.get<Pose>(guest).position;
    park.run_seconds(2);
    REQUIRE(r.all_of<Floating>(guest));
    CHECK(r.get<Floating>(guest).along != along);
    CHECK(glm::distance(r.get<Pose>(guest).position, was) > 1.0f);
    // Off after half a lap to two, up the stairs into the pool they came from.
    const float lap = r.get<River>(river).course.length();
    REQUIRE(park.run_until([&] { return !r.all_of<Floating>(guest); }, (kTuning.rivers.most_stay * lap + lap) / (kTuning.rivers.flow.value - kTuning.rivers.most_paddle.value / 2) + 60));
    CHECK(r.get<Errand>(guest).kind == ErrandKind::Swimming);
    CHECK(r.get<CycleRide>(river).riders.empty());
    CHECK(in_swimwear(r, guest));
    const glm::vec3 square = r.get<River>(river).stations.front().square;
    CHECK(glm::distance(r.get<Pose>(guest).position, square) < 0.01f);
}

TEST_CASE("paddling along a lazy river's current speeds a rider up, and against it slows them")
{
    Sandbox park;
    const auto river = river_by_pool(park);
    auto& r = park.world().registry;
    park.run(1); // Joins the station to the pool
    const entt::entity room = r.view<const PoolDoor>().front();
    const auto float_one = [&](float facing) {
        const auto guest = park.guest({ 0, 0 });
        r.emplace_or_replace<Swimmer>(guest, Swimmer { .changed = true, .left = { 300 }, .in_water = true, .room = room });
        r.get<Errand>(guest) = { .kind = ErrandKind::Riding, .target = river };
        r.emplace<Floating>(guest, Floating { .river = river, .stay = 1000, .lane = 0, .facing = { facing }, .paddle = { 1.0f }, .paddling = true });
        return guest;
    };
    const auto along = float_one(0);
    const auto against = float_one(std::numbers::pi_v<float>);
    park.run(1);
    const float flow = kTuning.rivers.flow.value * kTickSeconds;
    CHECK(r.get<Floating>(along).along == doctest::Approx(flow + kTickSeconds).epsilon(0.01));
    CHECK(r.get<Floating>(against).along == doctest::Approx(flow - kTickSeconds).epsilon(0.01));
}

TEST_CASE("a lazy river's station joins the pool paving or pool path along its side, at its level, and riders board from it")
{
    using K = park::PoolKind;
    for (const K kind : { K::Tile, K::Path, K::PathStairs })
    {
        Sandbox park;
        const auto river = river_by_pool(park, kind);
        auto& r = park.world().registry;
        park.run(1);
        const auto& station = r.get<River>(river).stations.front();
        REQUIRE(r.valid(station.pool));
        const auto& paths = park.world().paths;
        CHECK(station.square.x == doctest::Approx(paths.origin.x + 7.5f * paths.tile_size));
        CHECK(station.square.z == doctest::Approx(paths.origin.y + 3.5f * paths.tile_size));
        // A swimmer gets on from that square and, far enough round, off onto it.
        const auto guest = park.guest({ 0, 0 }, { .swim = 0.9f, .cash = 1000 });
        REQUIRE(park.run_until([&] { return r.all_of<Floating>(guest); }, 900));
        const float lap = r.get<River>(river).course.length();
        REQUIRE(park.run_until([&] { return !r.all_of<Floating>(guest); }, (kTuning.rivers.most_stay * lap + lap) / (kTuning.rivers.flow.value - kTuning.rivers.most_paddle.value / 2) + 60));
        CHECK(glm::distance(r.get<Pose>(guest).position, station.square) < 0.01f);
    }
    // Paving a level up does not join it.
    {
        Sandbox park;
        const auto river = river_by_pool(park, K::Tile, 2);
        park.run(1);
        CHECK_FALSE(park.world().registry.valid(park.world().registry.get<River>(river).stations.front().pool));
    }
    // Nor does paving a square off from the station's side, nor one past its end; a wide station, its track half a
    // square further off, does join it.
    Sandbox park;
    const auto river = river_by_pool(park, K::Tile);
    const float tile = park.world().paths.tile_size;
    std::vector<glm::vec3> points = park.world().registry.get<River>(river).stations.front().points;
    REQUIRE(pool_beside(park.world(), points));
    const auto moved = [&](glm::vec3 by) {
        std::vector<glm::vec3> m = points;
        for (glm::vec3& p : m)
            p += by * tile;
        return pool_beside(park.world(), m).has_value();
    };
    CHECK_FALSE(moved({ 1, 0, 0 }));
    CHECK(moved({ 0.5f, 0, 0 }));
    CHECK_FALSE(moved({ 0, 0, 2.5f }));
}
