// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3::simulation;

namespace
{
    bool near(const glm::vec3& a, const glm::vec3& b)
    {
        return glm::distance(a, b) < 0.01f;
    }
} // namespace

TEST_CASE("riders walk past the ticket spot to their seats, and off past the exit")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 }).path({ 4, 1 }, { 4, 3 });
    const auto ride = park.flat_ride("Twirl", { 4, 0 }, 100, { .capacity = 2, .cycle_seconds = 10, .exit = glm::ivec2 { 4, 3 } });
    auto& r = park.world().registry;
    const auto& paths = park.world().paths;
    const glm::vec3 entrance = paths.centre(r.get<Attraction>(ride).entrance);
    const glm::vec3 ticket = entrance + glm::vec3(1, 0, 0);
    const glm::vec3 seat = entrance + glm::vec3(6, 1, 0);
    const glm::vec3 way_out = entrance + glm::vec3(6, 0, 8);
    r.emplace<RideWalks>(ride, RideWalks { .ticket = ticket, .ticket_heading = 1, .on = { { entrance + glm::vec3(4, 0, 0), seat } }, .off = { { seat, way_out } } });
    const auto g = park.guest({ 4, 0 }, { .cash = 1000 });
    r.get<Errand>(g) = { ErrandKind::Heading, ride, 0 };

    // Pays, walks to the ticket spot and stands there facing the way given, the ride waiting.
    REQUIRE(park.run_until([&] { return r.all_of<RideWalk>(g) && r.get<RideWalk>(g).next == 1; }, 30));
    CHECK(r.get<Errand>(g).kind == ErrandKind::Riding);
    CHECK(r.get<Wallet>(g).cash.value == 900);
    CHECK(near(r.get<Pose>(g).position, ticket));
    const std::uint64_t at_ticket = park.world().tick;
    park.run(2);
    CHECK(r.get<CycleRide>(ride).phase == RidePhase::Loading);
    CHECK(r.get<Pose>(g).heading.value == doctest::Approx(1.0f));
    CHECK(near(r.get<Pose>(g).position, ticket));

    // On to the seat, and only then off it goes.
    REQUIRE(park.run_until([&] { return !r.all_of<RideWalk>(g); }, 30));
    CHECK(near(r.get<Pose>(g).position, seat));
    CHECK(park.world().tick - at_ticket >= ticks(kTuning.queues.ticket.value));
    CHECK(park.run_until([&] { return r.get<CycleRide>(ride).phase != RidePhase::Loading; }, 1));

    // Off the seat on foot once the ride is done, still riding till on the paths at the exit.
    REQUIRE(park.run_until([&] { return r.all_of<RideWalk>(g); }, 60));
    CHECK(r.get<RideWalk>(g).off);
    CHECK(r.get<Errand>(g).kind == ErrandKind::Riding);
    CHECK(r.get<CycleRide>(ride).riders.empty());
    REQUIRE(park.run_until([&] { return r.get<Errand>(g).kind != ErrandKind::Riding; }, 30));
    CHECK_FALSE(r.all_of<RideWalk>(g));
    const glm::vec3 exit = paths.centre(r.get<CycleRide>(ride).exit);
    CHECK(glm::distance(glm::vec2(r.get<Pose>(g).position.x, r.get<Pose>(g).position.z), glm::vec2(exit.x, exit.z)) < 1.0f);
}

TEST_CASE("the next rider steps up to the ticket spot once the one ahead has handed theirs over")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = park.flat_ride("Twirl", { 4, 0 }, 100, { .capacity = 2, .cycle_seconds = 10 });
    auto& r = park.world().registry;
    const glm::vec3 entrance = park.world().paths.centre(r.get<Attraction>(ride).entrance);
    r.emplace<RideWalks>(ride, RideWalks { .ticket = entrance, .on = { { entrance + glm::vec3(20, 0, 0) }, { entrance + glm::vec3(20, 0, 1) } } });
    const auto a = park.guest({ 4, 0 }, { .cash = 1000 });
    const auto b = park.guest({ 4, 0 }, { .cash = 1000 });
    r.get<Errand>(a) = { ErrandKind::Heading, ride, 0 };
    r.get<Errand>(b) = { ErrandKind::Heading, ride, 0 };
    REQUIRE(park.run_until([&] { return r.all_of<RideWalk>(a) || r.all_of<RideWalk>(b); }, 30));
    const auto first = r.all_of<RideWalk>(a) ? a : b;
    const auto second = first == a ? b : a;
    CHECK(r.get<Errand>(second).kind == ErrandKind::Queueing);
    // Still queueing while the first hands over their ticket; boards as the first walks on.
    REQUIRE(park.run_until([&] { return r.get<RideWalk>(first).pause.value <= 0; }, 10));
    CHECK(park.run_until([&] { return r.all_of<RideWalk>(second); }, 1));
    CHECK(r.get<RideWalk>(first).next >= 1);
    // Both seated, in seats of their own, before it goes.
    REQUIRE(park.run_until([&] { return !r.all_of<RideWalk>(a) && !r.all_of<RideWalk>(b); }, 60));
    CHECK(glm::distance(r.get<Pose>(a).position, r.get<Pose>(b).position) == doctest::Approx(1.0f));
}

TEST_CASE("a load walks off abreast, each to a side of the way out within the way's width, and back to the path's middle")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 }).path({ 4, 1 }, { 4, 3 });
    const auto ride = park.flat_ride("Twirl", { 4, 0 }, 100, { .capacity = 3, .cycle_seconds = 10, .exit = glm::ivec2 { 4, 3 } });
    auto& r = park.world().registry;
    const glm::vec3 entrance = park.world().paths.centre(r.get<Attraction>(ride).entrance);
    const glm::vec3 bend = entrance + glm::vec3(6, 0, 4);
    std::vector<std::vector<glm::vec3>> on, off;
    for (int seat = 0; seat < 3; ++seat)
    {
        on.push_back({ entrance + glm::vec3(6, 0, static_cast<float>(seat)) });
        off.push_back({ on.back().front(), bend, bend + glm::vec3(0, 0, 4) });
    }
    r.emplace<RideWalks>(ride, RideWalks { .ticket = entrance, .on = on, .off = off });
    std::vector<entt::entity> load;
    for (int i = 0; i < 3; ++i)
    {
        load.push_back(park.guest({ 4, 0 }, { .cash = 1000 }));
        r.get<Errand>(load.back()) = { ErrandKind::Heading, ride, 0 };
    }
    REQUIRE(park.run_until([&] { return std::ranges::all_of(load, [&](entt::entity g) { return r.all_of<RideWalk>(g) && r.get<RideWalk>(g).off; }); }, 120));
    const glm::vec3 exit = park.world().paths.centre(r.get<CycleRide>(ride).exit);
    std::vector<float> sides;
    for (const entt::entity g : load)
    {
        const auto& points = r.get<RideWalk>(g).points;
        REQUIRE(points.size() == 4);
        // Off along +z, so a side is along x; the seat stays where it is and the path join on its middle.
        sides.push_back(points[1].x - bend.x);
        CHECK(std::abs(sides.back()) <= kTuning.queues.reach.value * kTuning.queues.way_out.value + 1e-4f);
        CHECK(std::abs(sides.back()) > 0);
        CHECK(near(points.back(), exit));
    }
    CHECK(sides[0] != sides[1]);
    CHECK(sides[1] != sides[2]);
    CHECK(sides[0] != sides[2]);
}
