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
