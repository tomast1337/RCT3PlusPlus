// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/coaster.hpp"

#include "simulation/ratings.hpp"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <numbers>
#include <vector>

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

    /// A 100 m track with two ends: a 30 m spike up 25 m at each, and a 20 m station from 40 to 60 between.
    Coaster spikes(Shuttle shuttle, int passes)
    {
        Coaster c;
        c.distance = { 0, 30, 40, 60, 70, 100 };
        c.height = { 25, 0, 0, 0, 0, 25 };
        c.kinds = { Stretch::Plain, Stretch::Plain, Stretch::Station, Stretch::Plain, Stretch::Plain, Stretch::Plain };
        c.station_speed = 2;
        c.train_length = 5;
        c.start = 60;
        c.at = 60;
        c.shuttle = shuttle;
        c.passes = passes;
        c.launch_speed = 10;
        c.running = true;
        return c;
    }

    /// A 60 m tower straight up from a 5 m base.
    Coaster tower(Shuttle shuttle, int passes)
    {
        Coaster c;
        c.distance = { 0, 5, 65 };
        c.height = { 0, 5, 65 };
        c.kinds = { Stretch::Station, Stretch::Plain, Stretch::Plain };
        c.station_speed = 2;
        c.train_length = 3;
        c.start = 5;
        c.at = 5;
        c.shuttle = shuttle;
        c.passes = passes;
        c.launch_speed = 20;
        c.running = true;
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

TEST_CASE("a slide's car down in the pool on its way back is slowed in the water and carried on to the lift")
{
    // The drop's foot to the lift's foot is the pool, the level 30 m back from 70 m.
    Coaster c = hill();
    c.wades = { { { 70, 100 } } };
    c.running = true;
    float in_pool = 0;
    float slowest = 100;
    for (int t = 0; t < 4000 && c.running; ++t)
    {
        drive(c, 0.01f);
        if (c.at > 75 && c.at < 100)
        {
            in_pool = std::max(in_pool, c.speed);
            slowest = std::min(slowest, c.speed);
        }
    }
    // Some 17 m/s off the drop, under 3 m/s once 5 m into the water, never stopping.
    CHECK(in_pool < 3);
    CHECK(slowest > 2.5f);
    CHECK(c.at == 10);
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

TEST_CASE("brakes slow a train down along them to their speed and no further")
{
    // Down the drop at about 17 m/s onto 30 m of brakes set to 4.
    Coaster c = hill();
    c.kinds[3] = Stretch::Brake;
    c.brake_speeds = { -1, -1, -1, 4, -1 };
    c.running = true;
    while (c.at < 71)
        drive(c, 0.05f);
    CHECK(c.speed > 10); // Not cut at once
    float slowest = c.speed;
    while (c.at < 99)
    {
        drive(c, 0.05f);
        slowest = std::min(slowest, c.speed);
    }
    CHECK(slowest == doctest::Approx(4).epsilon(0.05));
    CHECK(c.speed < 4.5f);
}

TEST_CASE("brakes slow a train less the fewer of its cars are over them")
{
    Coaster c = hill();
    c.height = { 0, 0, 0, 0, 0 };
    c.kinds = { Stretch::Station, Stretch::Plain, Stretch::Plain, Stretch::Brake, Stretch::Plain };
    c.brake_speeds = { -1, -1, -1, 4, -1 };
    c.cars = 4;
    c.train_length = 8;
    c.running = true;
    c.launched = true; // Long gone from its platform
    c.travelled = 20;
    c.at = 71; // One car on the brakes
    c.speed = 15;
    drive(c, 0.1f);
    const float one = 15 - c.speed;
    c.at = 79; // Every car on them
    c.speed = 15;
    drive(c, 0.1f);
    const float all = 15 - c.speed;
    CHECK(one > 0);
    CHECK(all == doctest::Approx(4 * one).epsilon(0.1));
}

TEST_CASE("brakes never speed a train up, nor stop one when set to nothing")
{
    Coaster c = hill();
    c.height = { 0, 0, 0, 0, 0 };
    c.kinds = { Stretch::Station, Stretch::Plain, Stretch::Plain, Stretch::Brake, Stretch::Plain };
    c.brake_speeds = { -1, -1, -1, 6, -1 };
    c.running = true;
    c.launched = true;
    c.travelled = 20;
    c.at = 80;
    c.speed = 3;
    drive(c, 0.5f);
    CHECK(c.speed <= 3);
    CHECK(c.speed > 2.5f);
    c.brake_speeds[3] = 0;
    c.at = 80;
    c.speed = 12;
    drive(c, 0.5f);
    CHECK(c.speed > 11.5f);
}

TEST_CASE("a block brake slows a passing train to its speed with the block ahead clear")
{
    Coaster c = hill();
    c.kinds[3] = Stretch::BlockBrake;
    c.brake_speeds = { -1, -1, -1, 4, -1 };
    c.running = true;
    while (c.at < 99)
        drive(c, 0.05f);
    CHECK(c.speed == doctest::Approx(4).epsilon(0.05));
}

TEST_CASE("a block brake pushes a slow train on up to its speed, and no faster")
{
    Coaster c = hill();
    c.height = { 0, 0, 0, 0, 0 };
    c.kinds = { Stretch::Station, Stretch::Plain, Stretch::Plain, Stretch::BlockBrake, Stretch::Plain };
    c.brake_speeds = { -1, -1, -1, 4, -1 };
    c.running = true;
    c.launched = true;
    c.travelled = 20;
    c.at = 80; // Standing on it, let go
    c.speed = 0;
    drive(c, 0.1f);
    CHECK(c.speed > 0);
    CHECK(c.speed < 4);
    for (int i = 0; i < 20; ++i)
        drive(c, 0.1f);
    CHECK(c.speed == doctest::Approx(4).epsilon(0.05));
}

TEST_CASE("booster jets shove a car up a climb, hard, to their speed and no faster")
{
    // A 20 m climb up 8 m from 10 m on, boosters along it set to 9 m/s.
    Coaster c = hill();
    c.distance = { 0, 10, 30, 40 };
    c.height = { 0, 0, 8, 8 };
    c.kinds = { Stretch::Station, Stretch::Booster, Stretch::Plain, Stretch::Plain };
    c.brake_speeds = { -1, 9, -1, -1 };
    c.train_length = 2;
    c.cars = 1;
    c.running = true;
    c.launched = true;
    c.travelled = 5;
    c.at = 12;
    c.speed = 1;
    drive(c, 0.5f);
    CHECK(c.speed > 5); // Much harder than any station or block brake pushes
    for (int i = 0; i < 10; ++i)
        drive(c, 0.1f);
    CHECK(c.speed <= 9);
    CHECK(c.speed > 8);
}

TEST_CASE("a pool slide's car is eased off its platform, then taken by the slope")
{
    // A 4 m platform, then a 20 m slope down 10 m.
    Coaster c;
    c.distance = { 0, 4, 24 };
    c.height = { 10, 10, 0 };
    c.kinds = { Stretch::Station, Stretch::Plain, Stretch::Plain };
    c.station_speed = 4;
    c.train_length = 2.5f;
    c.start = 4;
    c.at = 4;
    c.closed = false;
    c.splash_out = true;
    c.running = true;
    drive(c, 0.1f);
    CHECK(c.speed > 0);
    CHECK(c.speed < 1); // Eased up, not put straight to the station's speed
    float most = 0;
    while (c.travelled < 0.5f && c.running)
    {
        drive(c, 0.05f);
        most = std::max(most, c.speed);
    }
    CHECK(most < 1.5f); // Nothing like the station speed it would be carried at, though the slope has begun to tell
    for (int i = 0; i < 20 && c.running; ++i)
        drive(c, 0.05f);
    CHECK(c.speed > 2); // Gravity's, down the slope
}

TEST_CASE("a train held at a block brake is braked to a stop where its room ends, not stopped dead")
{
    Coaster c = hill();
    c.height = { 0, 0, 0, 0, 0 };
    c.kinds = { Stretch::Station, Stretch::Plain, Stretch::Plain, Stretch::BlockBrake, Stretch::Plain };
    c.brake_speeds = { -1, -1, -1, 4, -1 };
    c.running = true;
    c.launched = true;
    c.travelled = 20;
    c.at = 75;
    c.speed = 4;
    const float end = 85; // The room ends 10 m on
    float last = c.speed;
    bool smooth = true;
    for (int i = 0; i < 400 && c.speed > 0; ++i)
    {
        drive(c, 0.05f, end - c.at);
        smooth = smooth && last - c.speed < 1.5f; // No sudden stop: no harder than 30 m/s² a step
        last = c.speed;
    }
    CHECK(smooth);
    CHECK(c.speed == 0);
    CHECK(c.at == doctest::Approx(end).epsilon(0.01));
}

TEST_CASE("a train coming into the station slows the nearer it gets to where it stops, and creeps in")
{
    Coaster c = hill();
    c.running = true;
    for (int i = 0; i < 2000 && c.travelled < 90; ++i)
        drive(c, 0.05f);
    drive(c, 0.05f);
    REQUIRE(c.running);
    const float in = c.speed; // As the front comes into the station, under 10 m from its stop
    CHECK(in > c.station_speed);
    CHECK(in < 8);
    float last = in;
    bool slowing = true;
    for (int i = 0; i < 400 && c.running && c.travelled < 99.5f; ++i)
    {
        drive(c, 0.05f);
        slowing = slowing && c.speed <= last + 0.01f;
        last = c.speed;
    }
    CHECK(slowing);
    CHECK(last < c.station_speed); // Creeping in for the last of the way
}

TEST_CASE("a launched train that rolls back into the station stops there and is not launched again")
{
    // Too slow a launch for the 15 m hill: it rolls back down.
    Coaster c = hill();
    c.kinds[1] = Stretch::Plain;
    c.launch_speed = 8;
    c.running = true;
    float top = 0;
    bool back = false;
    for (int i = 0; i < 4000 && !back; ++i)
    {
        back = drive(c, 0.05f);
        top = std::max(top, c.speed);
    }
    CHECK(back);
    CHECK(top < 8.5f);
    CHECK_FALSE(c.running);
    CHECK(c.at == 10);
}

TEST_CASE("a reverse launched shuttle goes back up one spike, through the station up the other, and stops in it")
{
    Coaster c = spikes(Shuttle::Back, 2);
    float lowest = c.at;
    float highest = c.at;
    float first = 0; // Its speed a moment after setting off
    bool back = false;
    for (int i = 0; i < 4000 && !back; ++i)
    {
        back = drive(c, 0.05f);
        first = i == 10 ? c.speed : first;
        lowest = std::min(lowest, c.at);
        highest = std::max(highest, c.at);
    }
    CHECK(back);
    CHECK(first < 0);
    CHECK(lowest < 40 - 5);  // Up the spike behind the station
    CHECK(highest > 70 + 5); // and the one ahead of it
    CHECK(c.at == 60);
    CHECK(c.passed == 2);
    CHECK_FALSE(c.running);
}

TEST_CASE("a shuttle's chain carries the train up it going backwards too")
{
    Coaster c = spikes(Shuttle::Back, 2);
    // The spike behind the station is longer, its chain stopping 10 m short of the top.
    c.distance = { 0, 10, 40, 50, 70, 80, 110 };
    c.height = { 25, 22, 0, 0, 0, 0, 25 };
    c.kinds = { Stretch::Plain, Stretch::Chain, Stretch::Plain, Stretch::Station, Stretch::Plain, Stretch::Plain, Stretch::Plain };
    c.start = c.at = 70;
    c.lift_speed = 3;
    c.launch_speed = 4; // Too slow to climb far up the spike on its own
    float lowest = c.at;
    bool back = false;
    for (int i = 0; i < 4000 && !back; ++i)
    {
        back = drive(c, 0.05f);
        lowest = std::min(lowest, c.at);
    }
    CHECK(back);
    CHECK_FALSE(c.crashed);
    CHECK(lowest < 15 + 2); // Carried all the way up the chain, its back past the chain's top
}

TEST_CASE("a swinging shuttle is pushed each pass, building up and dying down, and stops after its passes")
{
    Coaster c = spikes(Shuttle::Swing, 8);
    std::vector<float> fastest(9, 0); // On each pass
    bool back = false;
    for (int i = 0; i < 20000 && !back; ++i)
    {
        const int pass = c.passed;
        back = drive(c, 0.05f);
        fastest[static_cast<std::size_t>(pass)] = std::max(fastest[static_cast<std::size_t>(pass)], std::abs(c.speed));
    }
    CHECK(back);
    CHECK(c.at == 60);
    CHECK(fastest[0] < fastest[3]);
    CHECK(fastest[3] == doctest::Approx(fastest[4]).epsilon(0.15));
}

TEST_CASE("a launched tower's train is braked as it falls back, then pushed up again but less high")
{
    Coaster c = tower(Shuttle::Tower, 2);
    std::vector<float> top(3, 0); // Highest on each pass
    bool back = false;
    for (int i = 0; i < 4000 && !back; ++i)
    {
        back = drive(c, 0.05f);
        top[static_cast<std::size_t>(std::min(c.passed, 2))] = std::max(top[static_cast<std::size_t>(std::min(c.passed, 2))], c.at);
    }
    CHECK(back);
    CHECK(c.at == 5);
    CHECK(top[0] > 15);
    CHECK(top[1] > 10);
    CHECK(top[1] < top[0]);
}

TEST_CASE("a drop tower's train is held at the top of its climb before it falls")
{
    Coaster c = tower(Shuttle::Drop, 1);
    float still = 0; // Seconds stopped up the tower
    bool back = false;
    for (int i = 0; i < 4000 && !back; ++i)
    {
        back = drive(c, 0.05f);
        still += c.speed == 0 && c.at > 10 ? 0.05f : 0;
    }
    CHECK(back);
    CHECK(still == doctest::Approx(3).epsilon(0.1));
    Coaster plain = tower(Shuttle::Tower, 1);
    float plain_still = 0;
    for (int i = 0; i < 4000 && plain.running; ++i)
    {
        drive(plain, 0.05f);
        plain_still += plain.speed == 0 && plain.at > 10 ? 0.05f : 0;
    }
    CHECK(plain_still < 0.2f);
}

namespace
{
    /// The hill circuit left unfinished: its track stops 10 m past the bottom of the drop.
    Coaster unfinished()
    {
        Coaster c = hill();
        c.distance = { 0, 10, 40, 70, 80 };
        c.height = { 0, 0, 15, 0, 0 };
        c.closed = false;
        c.running = true;
        return c;
    }
} // namespace

TEST_CASE("a train going round an unfinished track runs off its end and crashes there, still going")
{
    Coaster c = unfinished();
    bool back = false;
    for (int i = 0; i < 4000 && c.running; ++i)
        back = back || drive(c, 0.05f);
    CHECK_FALSE(back);
    CHECK(c.crashed);
    CHECK(c.at == 80);
    CHECK(c.speed > 5);
}

TEST_CASE("a train off the end of a pool slide splashes out, not crashed, still going, and that ends its lap")
{
    Coaster c = unfinished();
    c.splash_out = true;
    bool back = false;
    for (int i = 0; i < 4000 && c.running; ++i)
        back = drive(c, 0.05f);
    CHECK(back);
    CHECK(c.splashed);
    CHECK_FALSE(c.crashed);
    CHECK(c.at == 80);
    CHECK(c.speed > 5);
    CHECK_FALSE(standing_at(c)); // Not in the station to load
    Coaster tested = unfinished();
    tested.splash_out = true;
    const auto test = test_lap(tested); // Rated as a ride that comes round
    REQUIRE(test);
    CHECK(test->ride_length == doctest::Approx(80));
}

TEST_CASE("a shuttle's train pushed too hard runs off an end and crashes")
{
    Coaster c = spikes(Shuttle::Back, 2);
    c.launch_speed = 40; // Far more than the spike behind the station takes
    for (int i = 0; i < 4000 && c.running; ++i)
        drive(c, 0.05f);
    CHECK(c.crashed);
    CHECK(c.at == 5); // Its back off the start of the track
    CHECK(c.speed < 0);
}

TEST_CASE("a train on a finished circuit never crashes")
{
    Coaster c = hill();
    c.running = true;
    bool back = false;
    for (int i = 0; i < 4000 && !back; ++i)
        back = drive(c, 0.05f);
    CHECK(back);
    CHECK_FALSE(c.crashed);
    CHECK(lap_seconds(unfinished()) == std::nullopt);
}

TEST_CASE("a ghost train crashes off the end of an unfinished track as any train does")
{
    Coaster c = unfinished();
    c.ghost = true;
    for (int i = 0; i < 2000 && !c.crashed; ++i)
        drive(c, 0.05f);
    CHECK(c.crashed);
    CHECK_FALSE(c.running);
    CHECK(c.at == 80);
    CHECK(c.speed > 0); // Going as fast as it left
}

TEST_CASE("a train on a circuit with another station stops there, and goes on round when set off again")
{
    Coaster c = hill();
    c.fixed_speed = 4;
    c.stops = { 50 };
    c.running = true;
    for (int i = 0; i < 100 && c.running; ++i)
        CHECK_FALSE(drive(c, 0.25f));
    CHECK_FALSE(c.running);
    CHECK(standing_at(c) == 1);
    CHECK(c.travelled == doctest::Approx(50));
    CHECK(track_point(c, c.at).x == track_point(c, 60).x);
    c.running = true;
    bool back = false;
    for (int i = 0; i < 100 && !back; ++i)
        back = drive(c, 0.25f);
    CHECK(back);
    CHECK(standing_at(c) == 0);
    CHECK(c.next_stop == 0);
    // A lap is timed without its stops.
    const auto lap = lap_seconds(c);
    REQUIRE(lap);
    CHECK(*lap == doctest::Approx(25).epsilon(0.02));
}

TEST_CASE("a train held up by the block ahead a hair short of the station's end has come in, not still under way")
{
    Coaster c = hill();
    c.fixed_speed = 4;
    c.running = true;
    c.travelled = 95;
    c.at = 5;
    CHECK(drive(c, 2, 5 - 1e-4f)); // Its room ends at the station's end, as a block end ahead does
    CHECK_FALSE(c.running);
    CHECK(standing_at(c) == 0);
}

TEST_CASE("a train held up in the station by the one ahead is where its riders get off, short of the station's end")
{
    const auto held_at = [](float travelled) {
        Coaster c = hill();
        c.fixed_speed = 4;
        c.running = true;
        c.travelled = travelled;
        c.at = std::fmod(c.start + travelled, c.length());
        drive(c, 1, 3); // Held 3 m on
        return c;
    };
    Coaster in = held_at(90); // Its front at the station's start
    CHECK(in.running);
    CHECK(in.travelled == doctest::Approx(93));
    CHECK(held_in_station(in) == 0);
    CHECK_FALSE(held_in_station(held_at(80))); // Still on the track before it
    drive(in, 0.25f);                          // Moving up again
    CHECK_FALSE(held_in_station(in));
}

TEST_CASE("a lap's graphs take a sample each tenth of a second, newest first when the lap ends")
{
    Coaster c;
    c.distance = { 0, 100 };
    c.height = { 5, 5 };
    c.points = { { 0, 5, 0 }, { 0, 5, 100 } };
    c.kinds = { Stretch::Plain, Stretch::Plain };
    c.speed = 10;
    LapRecorder lap;
    for (int i = 0; i < 3; ++i)
    {
        c.at = static_cast<float>(i);
        record(lap, c, 0.06f);
        record(lap, c, 0.06f);
        c.speed += 1;
    }
    REQUIRE(lap.speed.size() == 3);
    CHECK(lap.speed[0] == doctest::Approx(10));
    CHECK(lap.altitude[0] == doctest::Approx(5));
    // Level and straight: one g up, nothing across.
    CHECK(lap.vertical[0] == doctest::Approx(9.81f));
    CHECK(lap.lateral[0] == doctest::Approx(0));
    const rct3::save::RideGraphs graphs = end_lap(lap);
    CHECK(graphs.speed == std::vector<float> { 12, 11, 10 });
    CHECK(lap.speed.empty());
}

TEST_CASE("a long lap's graphs keep its last samples")
{
    LapRecorder lap;
    lap.speed.assign(kLapSamples + 50, 1);
    lap.speed.back() = 7;
    const rct3::save::RideGraphs graphs = end_lap(lap);
    REQUIRE(graphs.speed.size() == kLapSamples);
    CHECK(graphs.speed.front() == 7);
}

TEST_CASE("a turn banked to suit the train's speed presses riders into their seats, not sideways")
{
    // A circle 20 m round its middle, banked 45 degrees in, taken at the speed whose pull into the turn is one g.
    constexpr float kRadius = 20;
    constexpr int kPoints = 361;
    Coaster c;
    for (int i = 0; i < kPoints; ++i)
    {
        const float angle = 2 * std::numbers::pi_v<float> * static_cast<float>(i) / (kPoints - 1);
        const glm::vec3 out { std::cos(angle), 0, std::sin(angle) };
        c.points.push_back(out * kRadius);
        c.ups.push_back(glm::normalize(glm::vec3(0, 1, 0) - out));
        c.distance.push_back(kRadius * angle);
        c.height.push_back(0);
        c.kinds.push_back(Stretch::Plain);
    }
    c.speed = std::sqrt(9.81f * kRadius);
    LapRecorder lap;
    for (int i = 0; i < 20; ++i)
    {
        c.at = static_cast<float>(i) * c.speed * 0.025f;
        record(lap, c, 0.025f);
    }
    REQUIRE_FALSE(lap.vertical.empty());
    CHECK(lap.vertical.back() == doctest::Approx(9.81f * std::numbers::sqrt2_v<float>).epsilon(0.02));
    CHECK(std::abs(lap.lateral.back()) < 0.2f);
    // Unbanked, the same turn pulls them sideways by a g.
    c.ups.clear();
    LapRecorder flat;
    for (int i = 0; i < 20; ++i)
    {
        c.at = static_cast<float>(i) * c.speed * 0.025f;
        record(flat, c, 0.025f);
    }
    CHECK(std::abs(flat.lateral.back()) == doctest::Approx(9.81f).epsilon(0.02));
    CHECK(flat.vertical.back() == doctest::Approx(9.81f).epsilon(0.02));
}

TEST_CASE("a train is drawn between where its last step began and ended, the short way round the end")
{
    Coaster c = hill();
    c.was_at = 10;
    c.at = 14;
    CHECK(between_drives(c, 0.5f) == doctest::Approx(12));
    CHECK(between_drives(c, 2) == doctest::Approx(14)); // Never past where the step ended
    c.was_at = c.length() - 1;
    c.at = 1;
    CHECK(between_drives(c, 0.5f) == doctest::Approx(c.length()));
}

namespace
{
    /// A pool slide's car going round a flat circle of radius `r` to its left at `speed`, swinging in its trough as a
    /// body slide's car does.
    Coaster circling(float r, float speed)
    {
        Coaster c;
        constexpr int kPoints = 64;
        for (int k = 0; k <= kPoints; ++k)
        {
            const float a = 2 * std::numbers::pi_v<float> * static_cast<float>(k) / kPoints;
            c.points.emplace_back(r * std::cos(a), 0, r * std::sin(a));
            c.distance.push_back(r * a);
            c.height.push_back(0);
            c.ups.emplace_back(0, 1, 0);
            c.kinds.push_back(Stretch::Plain);
        }
        c.train_length = 2;
        c.at = 2;
        c.fixed_speed = speed;
        c.speed = speed;
        c.running = true;
        c.launched = true;
        c.trough = Trough { .damping = 2, .centring = 1, .friction = 0.5f };
        return c;
    }
} // namespace

TEST_CASE("a slide's car swings up its trough's wall in a turn, the further the faster, and stays in")
{
    Coaster slow = circling(10, 9);
    Coaster fast = circling(10, 13);
    for (int i = 0; i < 80; ++i)
    {
        drive(slow, 0.05f);
        drive(fast, 0.05f);
    }
    // A gentle turn's pull is not enough to slide it up the wall against the wall's friction.
    Coaster gentle = circling(10, 5);
    for (int i = 0; i < 80; ++i)
        drive(gentle, 0.05f);
    CHECK(gentle.lean == 0);
    REQUIRE(slow.running);
    REQUIRE(fast.running);
    CHECK(std::abs(slow.lean) > 0.05f);
    CHECK(std::abs(fast.lean) > std::abs(slow.lean));
    CHECK(slow.lean * fast.lean > 0); // Both out of the turn, the same way
}

TEST_CASE("a slide's car taken round a tight turn too fast flies out of its trough and crashes, but not in a tube")
{
    Coaster c = circling(3, 22);
    for (int i = 0; i < 12 && c.running; ++i)
        drive(c, 0.05f);
    CHECK(c.crashed);
    CHECK_FALSE(c.running);
    Coaster tube = circling(3, 22);
    tube.covered.assign(tube.distance.size(), true);
    for (int i = 0; i < 12; ++i) // Short of coming round to its start
        drive(tube, 0.05f);
    CHECK_FALSE(tube.crashed);
    CHECK(tube.running);
}

TEST_CASE("a slide's car taken round onto its lift is not swung up the wall by the turn")
{
    Coaster c = circling(3, 22);
    c.steady.assign(c.distance.size(), true);
    for (int i = 0; i < 12; ++i)
        drive(c, 0.05f);
    CHECK_FALSE(c.crashed);
    CHECK(c.lean == 0);
}

TEST_CASE("a slide's car on the straight stays in the middle of its trough")
{
    Coaster c = hill();
    c.trough = Trough { .damping = 2, .centring = 1, .friction = 0.5f };
    c.points = { { 0, 0, 0 }, { 0, 0, 10 }, { 0, 15, 40 }, { 0, 0, 70 }, { 0, 0, 100 } };
    c.running = true;
    for (int i = 0; i < 200 && c.running; ++i)
    {
        drive(c, 0.05f);
        CHECK(c.lean == doctest::Approx(0).epsilon(1e-4));
    }
}

TEST_CASE("a car on the level is slowed by rolling as much of its weight as it is given, and by the air with speed")
{
    Coaster c = hill();
    c.kinds[1] = Stretch::Plain;
    c.height = { 0, 0, 0, 0, 0 };
    c.running = true;
    c.launched = true;
    c.at = 40;
    c.speed = 10;
    c.rolling = 0.06f;
    c.air = 0;
    drive(c, 1);
    CHECK(c.speed == doctest::Approx(10 - 0.06f * 9.81f).epsilon(0.01));
    Coaster windy = c;
    windy.speed = 10;
    windy.air = 0.01f;
    drive(windy, 0.1f);
    CHECK(windy.speed == doctest::Approx(10 - (0.06f * 9.81f + 0.01f * 100) * 0.1f).epsilon(0.01));
}

TEST_CASE("a car off a slide's end flies on, comes down on its water ahead and floats till it is gone ten seconds on")
{
    Flight f { .from = { 0, 3, 0 }, .way = { 0, 0, 1 }, .at = { 0, 3, 0 }, .velocity = { 0, 0, 8 }, .water = 1 };
    // From 2 m over the water it falls for sqrt(2 * 2 / g) seconds, going on at its speed.
    const float fall = std::sqrt(4 / 9.81f);
    CHECK(fly(f, fall / 2));
    CHECK(f.afloat < 0);
    CHECK(f.at.y == doctest::Approx(3 - 9.81f / 2 * fall * fall / 4));
    CHECK(f.at.z == doctest::Approx(4 * fall));
    // Nose down as it falls, levelling off once afloat.
    const float dived = flown(f, glm::mat4(1.0f))[2].y;
    CHECK(dived < 0);
    CHECK(fly(f, fall));
    CHECK(f.afloat >= 0);
    CHECK(f.at.y == doctest::Approx(1));
    CHECK(f.at.z > 8 * fall);
    CHECK(flown(f, glm::mat4(1.0f))[2].y > dived);
    CHECK(flown(f, glm::mat4(1.0f))[2].y < 0);
    // It drifts on slowing, then sinks and is gone.
    const float drifted = f.at.z;
    CHECK(fly(f, 1));
    CHECK(flown(f, glm::mat4(1.0f))[2].y == doctest::Approx(0).epsilon(1e-5));
    CHECK(f.at.z > drifted);
    CHECK(glm::length(f.velocity) < 8);
    float t = 0;
    while (fly(f, 0.1f) && t < 20)
        t += 0.1f;
    CHECK(t == doctest::Approx(kGone - 1 - fall * 1.5f - 0.1f).epsilon(0.05));
    CHECK(f.at.y < 1);
}

TEST_CASE("a car off a slide's end is stopped by its pool's far wall, and flies the same however its time is stepped")
{
    const Flight start { .from = { 0, 3, 0 }, .way = { 0, 0, 1 }, .at = { 0, 3, 0 }, .velocity = { 0, 0, 8 }, .water = 1, .speed = 8, .reach = 5 };
    Flight stepped = start;
    for (int i = 0; i < 20; ++i)
        fly(stepped, 0.25f);
    Flight whole = start;
    fly(whole, 5);
    CHECK(stepped.at.z == doctest::Approx(5));
    CHECK(stepped.at.x == doctest::Approx(0));
    CHECK(stepped.afloat >= 0);
    CHECK(glm::distance(stepped.at, whole.at) < 1e-3f);
    CHECK(stepped.velocity.z == 0);
}
