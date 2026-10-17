// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park_rating.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using simulation::RatingInput;
using simulation::RatingParts;
using simulation::RideRatingInput;

TEST_CASE("an empty park scores half its happiness and nothing else")
{
    const RatingParts p = simulation::rate_park({ });
    CHECK(p.happiness == 250);
    CHECK(p.rides == 0);
    CHECK(p.reliability == 0);
    CHECK(p.guests == 0);
    CHECK(p.total == 250);
}

TEST_CASE("each part of the score adds to the total with its sign")
{
    RatingInput in;
    in.guests = 750;
    in.guests_inside = 4;
    in.happiness = 3; // Three quarters happy on average
    in.rides = { { .downtime_percent = 0, .excitement = 7.0f, .intensity = 5.0f }, { .downtime_percent = 50, .excitement = { }, .intensity = { } } };
    in.old_litter = 10;
    const RatingParts p = simulation::rate_park(in);
    CHECK(p.happiness == 375);
    CHECK(p.rides == 146);      // A ride scoring in full, times the variety of two rides
    CHECK(p.reliability == 75); // Running three quarters of the time on average
    CHECK(p.guests == 275);     // Most of the part at 750 guests
    CHECK(p.litter == -30);
    CHECK(p.harder == 0);
    CHECK(p.total == 375 + 146 + 75 + 275 - 30);
}

TEST_CASE("a ride too intense for comfort scores less, and nothing past the range")
{
    RatingInput mild;
    mild.rides = { { .downtime_percent = 0, .excitement = 7.0f, .intensity = 7.0f } };
    RatingInput harsh = mild;
    harsh.rides[0].intensity = 9.0f;
    RatingInput brutal = mild;
    brutal.rides[0].intensity = 11.0f;
    CHECK(simulation::rate_park(harsh).rides * 2 == doctest::Approx(simulation::rate_park(mild).rides).epsilon(0.05));
    CHECK(simulation::rate_park(brutal).rides == 0);
}

TEST_CASE("the harder rating takes a share off, and the total stays in range")
{
    RatingInput in;
    in.harder = true;
    const RatingParts p = simulation::rate_park(in);
    CHECK(p.harder == -38);
    CHECK(p.total == 212);

    RatingInput high;
    high.guests = 100000;
    high.guests_inside = 1;
    high.happiness = 1;
    high.rides.assign(100, { .downtime_percent = 0, .excitement = 9.0f, .intensity = 6.0f });
    CHECK(simulation::rate_park(high).total == 1000);

    RatingInput low;
    low.old_litter = 1000;
    low.guests_inside = 1;
    CHECK(simulation::rate_park(low).litter == -500);
    CHECK(simulation::rate_park(low).total == 0);
}

TEST_CASE("the inputs are gathered from the park's guests, rides and litter")
{
    save::PeopleModel people;
    people.guests.resize(3);
    people.guests[0].in_park = false;
    people.guests[0].mood_now.happiness = 0.9f;
    people.guests[1].mood_now.happiness = 0.6f;
    people.guests[2].mood_now.happiness = 0.5f;
    people.litter.resize(2);
    people.litter[0].create_time = 10;
    people.litter[1].create_time = 300; // Too new to count

    save::RideModel rides;
    rides.rides.resize(3);
    rides.rides[2].kind = save::RideKind::Special; // Facilities are not rated
    rides.rides[0].breakdown.downtime_windows = { 16, 0, 0, 0, 0, 0, 0, 0 };
    rides.rides[0].assessment.valid = true;
    rides.rides[0].assessment.excitement = 3.5f;
    rides.rides[0].assessment.intensity = 4.5f;
    rides.rides[1].kind = save::RideKind::Flat;
    rides.rides[1].breakdown.saved_downtime_percent = 65.625f; // Wins over the windows

    const RatingInput in = simulation::rating_input(people, rides, 300);
    CHECK(in.guests_inside == 2);
    CHECK(in.happiness == doctest::Approx(1.1f));
    CHECK(in.old_litter == 1);
    REQUIRE(in.rides.size() == 2);
    CHECK(in.rides[0].downtime_percent == doctest::Approx(10.0f));
    CHECK(in.rides[0].excitement == 3.5f);
    CHECK(in.rides[0].intensity == 4.5f);
    CHECK(in.rides[1].downtime_percent == 65.625f);
    CHECK_FALSE(in.rides[1].excitement);
}

TEST_CASE("the rating moves half way towards each week's score")
{
    CHECK(simulation::next_rating(std::nullopt, 600) == doctest::Approx(0.6f));
    CHECK(simulation::next_rating(0.2f, 600) == doctest::Approx(0.4f));
    CHECK(simulation::next_rating(0.8f, 2000) == doctest::Approx(0.9f));
}
