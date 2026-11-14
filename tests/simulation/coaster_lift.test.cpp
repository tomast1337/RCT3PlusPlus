// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/coaster.hpp"

#include <doctest/doctest.h>
#include <limits>
#include <vector>

using namespace rct3::simulation;

namespace
{
    constexpr float kOpen = std::numeric_limits<float>::infinity();

    /// A 100 m circuit: a 10 m station, a lift tower whose car carries 20 m of track from the station's end to 26 m
    /// of rails going up from 14 m on, then a 30 m drop and level back home.
    Coaster tower_lift()
    {
        Coaster c;
        c.distance = { 0, 10, 14, 40, 70, 100 };
        c.height = { 0, 0, 1, 27, 0, 0 };
        c.kinds = { Stretch::Station, Stretch::Lift, Stretch::Lift, Stretch::Plain, Stretch::Plain, Stretch::Plain };
        c.lifts = { { .entry = 10, .from = 14, .to = 40, .carried = { 20 }, .up = { 4 }, .down = { 8 }, .drop = 1 } };
        c.lift_speed = 3;
        c.station_speed = 2;
        c.train_length = 10;
        c.start = 10;
        c.at = 10;
        return c;
    }

    /// Drives `c` in tenths of a second, its lift's car going back down between, till `until` holds or `most`
    /// seconds pass; the seconds it took.
    float drive_until(Coaster& c, std::vector<Carriage>& cars, auto until, float most = 120, float room = kOpen)
    {
        float t = 0;
        for (; t < most && !until(); t += 0.1f)
        {
            lower_carriages(c, cars, 0.1f);
            drive(c, 0.1f, room, cars);
        }
        return t;
    }
} // namespace

TEST_CASE("a train boards a lift's car, is carried up, let go at the top, and the car goes back down")
{
    Coaster c = tower_lift();
    std::vector<Carriage> cars = carriages_of(c);
    REQUIRE(cars.size() == 1);
    c.running = true;
    drive(c, 0.1f, kOpen, cars);
    REQUIRE(c.lifted);
    CHECK(c.lifted->phase == LiftPhase::Boarding);
    CHECK(cars[0].state == CarriageState::Taken);
    // It rolls to a stop short of the far end of the car's track and stands a moment before the car sets off.
    drive_until(c, cars, [&] { return c.lifted->phase != LiftPhase::Boarding; });
    CHECK(c.lifted->phase == LiftPhase::Engaging);
    CHECK(c.lifted->aboard.value == doctest::Approx(19.5f));
    CHECK(c.at == doctest::Approx(10));
    const float standing = drive_until(c, cars, [&] { return c.lifted->phase != LiftPhase::Engaging; });
    CHECK(standing == doctest::Approx(2).epsilon(0.1));
    // Carried up, no faster than the car climbs.
    drive_until(c, cars, [&] { return c.lifted->phase != LiftPhase::Rising; });
    // With the block ahead clear, it is let go as soon as it is up.
    CHECK(c.lifted->phase == LiftPhase::Releasing);
    CHECK(cars[0].at.value == doctest::Approx(40));
    CHECK(c.at == doctest::Approx(40));
    // Let go after a moment more, it rolls off and down; with its last car off, the car goes back down.
    drive_until(c, cars, [&] { return !c.lifted; });
    CHECK(cars[0].state == CarriageState::Lowering);
    CHECK(c.at > 40);
    CHECK(c.speed > 5);
    drive_until(c, cars, [&] { return cars[0].state == CarriageState::Waiting; });
    CHECK(cars[0].at.value == doctest::Approx(14));
    // And it comes round home.
    drive_until(c, cars, [&] { return !c.running; });
    CHECK(c.at == doctest::Approx(10));
}

TEST_CASE("a lift's car holds a train at the top while the block ahead is taken")
{
    Coaster c = tower_lift();
    std::vector<Carriage> cars = carriages_of(c);
    c.running = true;
    drive_until(c, cars, [&] { return c.lifted && c.lifted->phase == LiftPhase::Rising; });
    REQUIRE(c.lifted);
    drive_until(c, cars, [&] { return c.lifted->phase == LiftPhase::AtTop; }, 30, 0);
    drive_until(c, cars, [] { return false; }, 10, 0);
    CHECK(c.lifted->phase == LiftPhase::AtTop);
    CHECK(c.at == doctest::Approx(40));
    drive_until(c, cars, [&] { return c.lifted->phase == LiftPhase::Leaving; });
    CHECK(c.lifted->phase == LiftPhase::Leaving);
}

TEST_CASE("a train waits short of a lift till its car is back down")
{
    Coaster first = tower_lift();
    Coaster second = tower_lift();
    std::vector<Carriage> cars = carriages_of(first);
    first.running = true;
    drive(first, 0.1f, kOpen, cars);
    REQUIRE(first.lifted);
    // The second, set off from the station's start, comes to the lift while the car is away.
    second.start = 0;
    second.at = 5;
    second.travelled = 5;
    second.speed = 2;
    second.running = true;
    for (int k = 0; k < 100; ++k)
        drive(second, 0.1f, kOpen, cars);
    CHECK_FALSE(second.lifted);
    CHECK(second.at == doctest::Approx(10));
    cars[0] = { };
    cars[0].at = { 14 };
    drive(second, 0.1f, kOpen, cars);
    CHECK(second.lifted);
}

TEST_CASE("without its car known, a lift tower pulls a train up as a chain does")
{
    Coaster c = tower_lift();
    c.running = true;
    float t = 0;
    for (; t < 120 && c.running; t += 0.1f)
        drive(c, 0.1f);
    CHECK_FALSE(c.lifted);
    CHECK_FALSE(c.running);
    CHECK(t < 60);
}
