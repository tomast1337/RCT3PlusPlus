// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

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
    CHECK(r.get<Wallet>(g).cash == 900);
    CHECK(near(r.get<Pose>(g).position, ticket));
    const std::uint64_t at_ticket = park.world().tick;
    park.run(2);
    CHECK(r.get<CycleRide>(ride).phase == RidePhase::Loading);
    CHECK(r.get<Pose>(g).heading == doctest::Approx(1.0f));
    CHECK(near(r.get<Pose>(g).position, ticket));

    // On to the seat, and only then off it goes.
    REQUIRE(park.run_until([&] { return !r.all_of<RideWalk>(g); }, 30));
    CHECK(near(r.get<Pose>(g).position, seat));
    CHECK(park.world().tick - at_ticket >= ticks(kTuning.queues.ticket_seconds));
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
    REQUIRE(park.run_until([&] { return r.get<RideWalk>(first).pause <= 0; }, 10));
    CHECK(park.run_until([&] { return r.all_of<RideWalk>(second); }, 1));
    CHECK(r.get<RideWalk>(first).next >= 1);
    // Both seated, in seats of their own, before it goes.
    REQUIRE(park.run_until([&] { return !r.all_of<RideWalk>(a) && !r.all_of<RideWalk>(b); }, 60));
    CHECK(glm::distance(r.get<Pose>(a).position, r.get<Pose>(b).position) == doctest::Approx(1.0f));
}
