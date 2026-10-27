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
