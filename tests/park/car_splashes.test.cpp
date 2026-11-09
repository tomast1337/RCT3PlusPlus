// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/car_splashes.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{

    // A closed course of three 10 m pieces, the middle one through water.
    track::TrackCourse course()
    {
        track::TrackCourse c;
        c.points = { { 0, 0, 0 }, { 10, 0, 0 }, { 20, 0, 0 }, { 30, 0, 0 } };
        c.distance = { 0, 10, 20, 30 };
        c.kinds.assign(4, track::CourseKind::Plain);
        c.piece_starts = { 0, 10, 20 };
        c.wet = { false, true, false };
        c.closed = true;
        return c;
    }

    constexpr float kStep = 0.1f;

} // namespace

TEST_CASE("a car splashes as it runs into water, once, and harder the faster it runs")
{
    const track::TrackCourse c = course();
    CarWater water;
    CHECK_FALSE(car_splash(water, c, 9, false, { 5, 0, 0 }, kStep).splash);
    const CarSplash in = car_splash(water, c, 10.5f, false, { 5, 0, 0 }, kStep);
    CHECK(in.splash);
    CHECK(in.wake);
    CHECK(water.splashes == 1);
    CHECK(water.strength == 0);
    CHECK_FALSE(car_splash(water, c, 11, false, { 5, 0, 0 }, kStep).splash);
    // Round again, faster.
    CHECK_FALSE(car_splash(water, c, 25, false, { 15, 0, 0 }, 2).splash);
    CHECK(car_splash(water, c, 40.5f, false, { 15, 0, 0 }, kStep).splash);
    CHECK(water.strength == 2);
}

TEST_CASE("a car splashes where water stops its fall short, but not crawling through or on dry track")
{
    const track::TrackCourse c = course();
    CarWater water;
    (void)car_splash(water, c, 12, false, { 2, 0, 0 }, kStep); // In slowly: no splash
    CHECK(water.splashes == 0);
    (void)car_splash(water, c, 13, false, { 8, -8, 0 }, 2);
    CHECK(car_splash(water, c, 14, false, { 9, 0, 0 }, kStep).splash);
    CarWater dry;
    (void)car_splash(dry, c, 3, false, { 8, -8, 0 }, kStep);
    CHECK_FALSE(car_splash(dry, c, 4, false, { 9, 0, 0 }, kStep).splash);
}

TEST_CASE("a car running on through water leaves a wake now and then")
{
    const track::TrackCourse c = course();
    CarWater water { .wet = true };
    int wakes = 0;
    for (int i = 0; i < 20; ++i)
        wakes += car_splash(water, c, 10.2f + 0.4f * static_cast<float>(i), false, { 4, 0, 0 }, kStep).wake ? 1 : 0;
    CHECK(wakes >= 3);
    CHECK(wakes <= 5);
}

TEST_CASE("a car splashes as it dives into the park's water off its track's own")
{
    const track::TrackCourse c = course();
    CarWater water;
    CHECK_FALSE(car_splash(water, c, 3, false, { 6, -6, 0 }, kStep).splash);
    CHECK(car_splash(water, c, 4, true, { 6, -6, 0 }, kStep).splash);
}
