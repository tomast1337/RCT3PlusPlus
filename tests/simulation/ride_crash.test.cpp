// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/coaster.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3::simulation;

namespace
{
    /// A ride whose 20 m of track along +x from the ride's entrance stops short of joining up: its train of two
    /// cars, for two riders, sets off at 5 m/s and is off the end in about three seconds.
    entt::entity unfinished(Sandbox& park)
    {
        const auto ride = park.flat_ride("Looper", { 4, 0 }, 100, { .capacity = 2, .cycle_seconds = 1000 });
        auto& r = park.world().registry;
        const glm::vec3 from = park.world().paths.centre(r.get<Attraction>(ride).entrance);
        Coaster train;
        train.distance = { 0, 20 };
        train.height = { 0, 0 };
        train.points = { from, from + glm::vec3(20, 0, 0) };
        train.kinds = { Stretch::Plain, Stretch::Plain };
        train.fixed_speed = 5;
        train.train_length = 4;
        train.cars = 2;
        train.closed = false;
        train.start = train.at = 4;
        r.emplace<Trains>(ride, Trains { { train }, { { } } });
        return ride;
    }

    bool thought(const entt::registry& r, entt::entity guest, ThoughtKind kind)
    {
        const auto* thoughts = r.try_get<Thoughts>(guest);
        return thoughts != nullptr && std::ranges::any_of(thoughts->recent, [&](const Thought& t) { return t.kind == kind; });
    }
} // namespace

TEST_CASE("a train off the end of its track crashes: the ride closes and its riders are thrown out hurt")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = unfinished(park);
    auto& r = park.world().registry;
    const auto g = park.guest({ 4, 0 }, { .cash = 1000 });
    r.get<Errand>(g) = { ErrandKind::Heading, ride, 0 };
    CHECK(park.run_until([&] { return r.all_of<Wrecks>(ride); }, 30));
    const auto& wrecks = r.get<Wrecks>(ride);
    CHECK(wrecks.crashes == 1);
    CHECK(wrecks.dangers == 2); // The crash of an open ride, and its car with a rider
    REQUIRE(wrecks.trains.size() == 1);
    CHECK(wrecks.trains[0].size() == 2);
    CHECK(wrecks.trains[0][0].velocity.x > 4); // Flying on the way the train went
    CHECK(r.get<Trains>(ride).trains[0].crashed);
    CHECK(r.get<Trains>(ride).riders[0].empty());
    CHECK_FALSE(r.get<Attraction>(ride).open);
    CHECK(r.get<Errand>(g).kind != ErrandKind::Riding);
    CHECK(r.all_of<Gesture>(g));
    CHECK(r.get<Gesture>(g).kind == GestureKind::Down);
    CHECK(thought(r, g, ThoughtKind::BeenInjured));
}

TEST_CASE("a guest near a crash thinks it a disaster, and one beside a wreck blowing up is knocked over")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = unfinished(park);
    auto& r = park.world().registry;
    r.get<Attraction>(ride).testing = true;
    const auto near = park.guest({ 3, 0 });
    CHECK(park.run_until([&] { return r.all_of<Wrecks>(ride); }, 30));
    CHECK(thought(r, near, ThoughtKind::Disaster));
    CHECK_FALSE(r.get<Attraction>(ride).testing);
    // Right by a wreck till it blows up.
    Wreck& wreck = r.get<Wrecks>(ride).trains[0][0];
    for (int i = 0; i < 200 && wreck.state == WreckState::Flying; ++i)
    {
        wreck.at.x = r.get<Pose>(near).position.x;
        wreck.at.z = r.get<Pose>(near).position.z;
        park.run(1);
    }
    CHECK(wreck.state == WreckState::Burning);
    CHECK(r.all_of<Gesture>(near));
    CHECK(thought(r, near, ThoughtKind::BeenInjured));
}

TEST_CASE("a crashed train is back in the station once its wrecks are gone and the ride opens again")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = unfinished(park);
    auto& r = park.world().registry;
    r.get<Attraction>(ride).testing = true;
    CHECK(park.run_until([&] { return r.all_of<Wrecks>(ride); }, 30));
    const auto& wrecks = r.get<Wrecks>(ride);
    CHECK(park.run_until([&] { return wrecks.trains[0].empty(); }, 30));
    park.run_seconds(5);
    CHECK(r.get<Trains>(ride).trains[0].crashed); // Still off the track while the ride stays closed
    r.get<Attraction>(ride).testing = true;
    park.run(1);
    const Coaster& train = r.get<Trains>(ride).trains[0];
    CHECK_FALSE(train.crashed);
    CHECK(train.travelled < 1);
}

TEST_CASE("a ghost train's crash flies its wrecks but hurts no one, closes nothing and is not counted")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = unfinished(park);
    auto& r = park.world().registry;
    r.get<Attraction>(ride).testing = true;
    r.get<Trains>(ride).trains[0].ghost = true;
    const auto near = park.guest({ 3, 0 });
    CHECK(park.run_until([&] { return r.all_of<Wrecks>(ride); }, 30));
    const auto& wrecks = r.get<Wrecks>(ride);
    REQUIRE(wrecks.trains.size() == 1);
    CHECK(wrecks.trains[0].size() == 2);
    CHECK(wrecks.trains[0][0].velocity.x > 4);
    CHECK(wrecks.trains[0][0].ghost);
    CHECK(wrecks.crashes == 0);
    CHECK(wrecks.dangers == 0);
    CHECK(wrecks.since.value == -1);
    CHECK(r.get<Attraction>(ride).testing);
    CHECK_FALSE(thought(r, near, ThoughtKind::Disaster));
    // Right by a wreck as it blows up, and still standing.
    Wreck& wreck = r.get<Wrecks>(ride).trains[0][0];
    for (int i = 0; i < 200 && wreck.state == WreckState::Flying; ++i)
    {
        wreck.at.x = r.get<Pose>(near).position.x;
        wreck.at.z = r.get<Pose>(near).position.z;
        park.run(1);
    }
    CHECK(wreck.state == WreckState::Burning);
    CHECK_FALSE(thought(r, near, ThoughtKind::BeenInjured));
    // Once its wrecks are gone, it sets off from the station again.
    CHECK(park.run_until([&] { return wrecks.trains[0].empty(); }, 30));
    park.run(1);
    CHECK_FALSE(r.get<Trains>(ride).trains[0].crashed);
}

TEST_CASE("the park forgets a crash after a while")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto ride = park.flat_ride("Looper", { 4, 0 }, 100);
    auto& r = park.world().registry;
    r.emplace<Wrecks>(ride, Wrecks { .since = { kTuning.crashes.forgotten.value - 2 } });
    park.run_seconds(1);
    CHECK(r.get<Wrecks>(ride).since.value > 0);
    park.run_seconds(2);
    CHECK(r.get<Wrecks>(ride).since.value == -1);
}
