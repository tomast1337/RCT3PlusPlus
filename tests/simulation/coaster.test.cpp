// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/coaster.hpp"

#include "simulation/ratings.hpp"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>

using namespace rct3::simulation;

namespace
{
    /// A 100 m circuit: a 10 m station, a 30 m chain up 15 m, a 30 m drop and 30 m level back home.
    Coaster hill()
    {
        Coaster c;
        c.distance = { 0, 10, 40, 70, 100 };
        c.height = { 0, 0, 15, 0, 0 };
        c.kinds = { Stretch::Station, Stretch::Chain, Stretch::Plain, Stretch::Plain, Stretch::Plain };
        c.lift_speed = 3;
        c.station_speed = 2;
        c.train_length = 5;
        c.start = 10;
        c.at = 10;
        return c;
    }
} // namespace

TEST_CASE("a train at fixed speed takes the track's length over that speed")
{
    Coaster c = hill();
    c.fixed_speed = 4;
    const auto lap = lap_seconds(c);
    REQUIRE(lap);
    CHECK(*lap == doctest::Approx(25).epsilon(0.02));
}

TEST_CASE("a launched train reaches its launch speed as its tail leaves the platform")
{
    Coaster c = hill();
    c.kinds[1] = Stretch::Plain; // No chain: the launch alone takes it on
    c.launch_speed = 20;
    c.running = true;
    for (int i = 0; i < 10000 && c.travelled < c.train_length; ++i)
        drive(c, 0.01f);
    CHECK(c.speed == doctest::Approx(20).epsilon(0.05));
    // Unlaunched, it only creeps off.
    Coaster slow = hill();
    slow.running = true;
    drive(slow, 1);
    CHECK(slow.speed < 5);
}

TEST_CASE("a launch stretch past the platform goes on pushing the train up its slope")
{
    Coaster plain = hill();
    plain.kinds[1] = Stretch::Plain;
    plain.launch_speed = 20;
    Coaster pushed = plain;
    pushed.kinds[1] = Stretch::Launch;
    for (Coaster* c : { &plain, &pushed })
    {
        c->running = true;
        for (int i = 0; i < 10000 && c->at < 35; ++i)
            drive(*c, 0.01f);
    }
    CHECK(pushed.speed > plain.speed + 2);
    CHECK(pushed.speed <= 20);
}

TEST_CASE("a train is pulled up the chain, runs down the drop faster and comes round to the station")
{
    const Coaster c = hill();
    const auto lap = lap_seconds(c);
    REQUIRE(lap);
    // Up the chain at its speed alone is 10 s; the drop and run home are quicker than that again.
    CHECK(*lap > 10);
    CHECK(*lap < 30);

    Coaster running = hill();
    running.running = true;
    float fastest = 0;
    bool back = false;
    for (int t = 0; t < 400 && !back; ++t)
    {
        back = drive(running, 0.25f);
        fastest = std::max(fastest, running.speed);
    }
    CHECK(back);
    CHECK(fastest > 10); // Dropping 15 m gives about 17 m/s before losses
    CHECK_FALSE(running.running);
    CHECK(running.at == 10);
}

TEST_CASE("a point on the track is on a chain only along the chain's stretch, round again past either end")
{
    const Coaster c = hill();
    CHECK_FALSE(chain_at(c, 5));
    CHECK(chain_at(c, 25));
    CHECK_FALSE(chain_at(c, 50));
    CHECK(chain_at(c, 125));
    CHECK(chain_at(c, -75));
}

TEST_CASE("a train with no chain to lift it is pushed on rather than left stuck in a dip")
{
    Coaster c = hill();
    c.kinds = { Stretch::Station, Stretch::Plain, Stretch::Plain, Stretch::Plain, Stretch::Plain };
    CHECK(lap_seconds(c));
}

TEST_CASE("a test run finds the coaster's speed, time and drops")
{
    const auto test = test_lap(hill());
    REQUIRE(test);
    CHECK(test->ride_length == 100);
    CHECK(test->drop_count == 1);
    CHECK(test->highest_drop == doctest::Approx(15).epsilon(0.1));
    CHECK(test->max_speed > 10);
    CHECK(test->average_speed == doctest::Approx(100 / test->ride_time));
}

TEST_CASE("brakes slow a train down to their speed and no further")
{
    Coaster c = hill();
    c.kinds[3] = Stretch::Brake;
    c.brake_speeds = { -1, -1, -1, 4, -1 };
    c.running = true;
    while (c.at < 71)
        drive(c, 0.1f);
    CHECK(c.speed <= 4.01f);
    CHECK(c.speed > 3);
}
