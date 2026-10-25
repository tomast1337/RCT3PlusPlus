// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/components.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3::simulation;

TEST_CASE("a guest picked up out of a queue leaves it, and those behind move up")
{
    Sandbox park;
    park.path({ 0, 0 }, { 2, 0 });
    park.queue({ 3, 0 }, { 6, 0 });
    const auto ride = park.flat_ride("Spinner", { 6, 0 }, 0, { .capacity = 1, .cycle_seconds = 60, .line = { { 6, 0 }, { 5, 0 }, { 4, 0 }, { 3, 0 } } });
    auto& r = park.world().registry;
    std::vector<entt::entity> guests;
    for (int i = 0; i < 3; ++i)
    {
        guests.push_back(park.guest({ 2, 0 }));
        r.get<Errand>(guests.back()) = { ErrandKind::Heading, ride, 0 };
    }
    CHECK(park.run_until([&] { return r.get<CycleRide>(ride).queue.size() == 2; }, 60));
    const entt::entity held = r.get<CycleRide>(ride).queue.front();
    REQUIRE(pick_up(park.world(), held));
    CHECK(r.all_of<Held>(held));
    CHECK_FALSE(r.any_of<Errand, Stride>(held));
    const auto& queue = r.get<CycleRide>(ride).queue;
    CHECK(std::ranges::find(queue, held) == queue.end());
    CHECK(queue.size() == 1);
    park.run_seconds(5); // The ride goes on without them
    CHECK(r.all_of<Held>(held));
}

TEST_CASE("a rider cannot be picked up, and a guest picked up thinks of it")
{
    Sandbox park;
    park.path({ 0, 0 }, { 2, 0 });
    const auto ride = park.flat_ride("Spinner", { 2, 0 }, 0, { .capacity = 4, .cycle_seconds = 60 });
    auto& r = park.world().registry;
    const auto rider = park.guest({ 2, 0 });
    r.get<Errand>(rider) = { ErrandKind::Heading, ride, 0 };
    CHECK(park.run_until([&] { return r.get<Errand>(rider).kind == ErrandKind::Riding; }, 20));
    CHECK_FALSE(pick_up(park.world(), rider));
    CHECK_FALSE(r.all_of<Held>(rider));

    const auto walker = park.guest({ 0, 0 });
    REQUIRE(pick_up(park.world(), walker));
    CHECK_FALSE(pick_up(park.world(), walker)); // Held already
    const auto kind = r.get<Thoughts>(walker).recent.front().kind;
    CHECK((kind == ThoughtKind::PickedUpHappy || kind == ThoughtKind::PickedUpNeutral || kind == ThoughtKind::PickedUpSad));
    CHECK(r.get<Held>(walker).height.value >= 3);
}

TEST_CASE("a party member waiting by a ride, picked up, leaves the wait and the party, and the rider goes on alone")
{
    Sandbox park;
    park.path({ 0, 0 }, { 6, 0 });
    const auto ride = park.flat_ride("Spinner", { 6, 0 }, 0, { .capacity = 4, .cycle_seconds = 20, .exit = glm::ivec2 { 3, 0 } });
    const auto leader = park.guest({ 6, 0 });
    const auto queasy = park.guest({ 6, 0 }, { .nausea = 0.9f }); // Too sick to ride
    const auto party = park.party({ leader, queasy });
    auto& r = park.world().registry;
    r.get<Errand>(leader) = { ErrandKind::Heading, ride, 0 };
    park.run(2);
    REQUIRE(r.get<Errand>(queasy).kind == ErrandKind::Waiting);
    REQUIRE(pick_up(park.world(), queasy));
    CHECK_FALSE(r.valid(party));
    CHECK_FALSE(r.any_of<Member>(leader));
    CHECK_FALSE(r.any_of<Member, Errand>(queasy));
    // Off the ride, the leader goes about alone rather than waiting for anyone.
    CHECK(park.run_until([&] { return r.get<Errand>(leader).kind == ErrandKind::Wandering; }, 60));
    park.run_seconds(5);
    CHECK(r.get<Errand>(leader).kind != ErrandKind::Waiting);
}

TEST_CASE("a party's leader picked up hands the lead to the next, whom the rest follow")
{
    Sandbox park;
    park.path({ 0, 0 }, { 8, 0 });
    const auto a = park.guest({ 0, 0 });
    const auto b = park.guest({ 0, 0 });
    const auto c = park.guest({ 0, 0 });
    const auto party = park.party({ a, b, c });
    auto& r = park.world().registry;
    REQUIRE(pick_up(park.world(), a));
    REQUIRE(r.valid(party));
    CHECK(r.get<Party>(party).leader == b);
    CHECK(r.get<Party>(party).members == std::vector<entt::entity> { b, c });
    CHECK(r.get<Errand>(b).kind == ErrandKind::Wandering);
    CHECK(r.get<Errand>(c).kind == ErrandKind::Following);
    CHECK_FALSE(r.any_of<Member>(a));
    park.run_seconds(30); // Walking on without them
    CHECK(r.get<Party>(party).members.size() == 2);
}

TEST_CASE("a person let go of falls to the ground and walks on from the nearest path")
{
    for (const bool on_path : { true, false })
    {
        Sandbox park;
        park.path({ 0, 0 }, { 4, 0 });
        auto& r = park.world().registry;
        const auto g = park.guest({ 0, 0 });
        REQUIRE(pick_up(park.world(), g));
        // Hung over the spot, as the player holds them.
        const glm::vec3 ground = on_path ? park.world().paths.centre(*park.world().paths.nearest({ 9, 0, 1 })) : park.world().paths.centre(*park.world().paths.nearest({ 9, 0, 1 })) + glm::vec3 { 0, 0, 12 };
        r.get<Pose>(g).position = ground + glm::vec3 { 0, r.get<Held>(g).height.value, 0 };
        REQUIRE(let_go(park.world(), g, ground));
        CHECK(r.all_of<Dropped>(g));
        park.run(2);
        CHECK(r.get<Pose>(g).position.y > ground.y); // Still falling
        CHECK(park.run_until([&] { return r.all_of<Errand, Stride>(g); }, 30));
        CHECK_FALSE(r.any_of<Held, Dropped>(g));
        const NodeId at = r.get<Stride>(g).from;
        CHECK(at == *park.world().paths.nearest({ 9, 0, 1 }));
        park.run_seconds(10);
        CHECK(park.world().paths.nearest(r.get<Pose>(g).position).has_value()); // Walking the paths again
    }
}

TEST_CASE("a mechanic picked up drops their job, and another is sent")
{
    Sandbox park;
    park.path({ 0, 0 }, { 8, 0 });
    const auto ride = park.flat_ride("Spinner", { 8, 0 }, 0);
    auto& r = park.world().registry;
    r.emplace_or_replace<Upkeep>(ride, Upkeep { .broken = true });
    const auto first = park.staff(StaffRole::Mechanic, { 0, 0 });
    const auto second = park.staff(StaffRole::Mechanic, { 0, 0 });
    CHECK(park.run_until([&] { return r.get<Upkeep>(ride).mechanic != entt::null; }, 30));
    const entt::entity sent = r.get<Upkeep>(ride).mechanic;
    REQUIRE(pick_up(park.world(), sent));
    CHECK(r.get<Staff>(sent).ride == entt::null);
    CHECK(park.run_until([&] { return r.get<Upkeep>(ride).mechanic == (sent == first ? second : first); }, 30));
}
