// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park_rating.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using simulation::RatingInput;
using simulation::RatingParts;
using simulation::RideRatingInput;

TEST_CASE("an empty park is rated on the middling happiness score alone")
{
    const RatingParts p = simulation::rate_park({ });
    CHECK(p.guests == 0);
    CHECK(p.happy_guests == 250);
    CHECK(p.ride_up_time == 0);
    CHECK(p.ride_scores == 0);
    CHECK(p.total == 250);
}

TEST_CASE("each part of the rating adds to the total with its sign")
{
    RatingInput in;
    in.harder = true;
    in.guests = 1000;
    in.guests_inside = 10;
    in.happy_guests = 7;
    in.rides = { { .downtime_percent = 0, .excitement = 4.0f, .intensity = 6.0f }, { .downtime_percent = 25, .excitement = { }, .intensity = { } } };
    in.old_litter = 3;
    in.penalty = 10;
    in.bonus = 20;
    const RatingParts p = simulation::rate_park(in);
    CHECK(p.base == -100);
    CHECK(p.guests == 75);        // Half way to the guest count that scores the most
    CHECK(p.happy_guests == 420); // Seven in ten happy
    CHECK(p.ride_up_time == 35);  // Three quarters up on average, scaled down for only two rides
    // Only the tested ride counts: its scores, and how near its averages are to the pleasant levels.
    CHECK(p.ride_scores == 5 + 48 + 8 + 45);
    CHECK(p.litter == -12);
    CHECK(p.penalty == -10);
    CHECK(p.bonus == 20);
    CHECK(p.total == -100 + 75 + 420 + 35 + 106 - 12 - 10 + 20);
}

TEST_CASE("every part stops at its cap")
{
    RatingInput in;
    in.guests = 5000;
    in.guests_inside = 10;
    in.happy_guests = 10;
    in.rides.assign(12, { .downtime_percent = 0, .excitement = 10.0f, .intensity = 10.0f });
    in.old_litter = 500;
    const RatingParts p = simulation::rate_park(in);
    CHECK(p.guests == 150);
    CHECK(p.happy_guests == 500);
    CHECK(p.ride_up_time == 200);
    CHECK(p.ride_scores == 100 + 11 + 100 + 20);
    CHECK(p.litter == -600);
}

TEST_CASE("the total is kept between nothing and the most there is")
{
    RatingInput high;
    high.bonus = 2000;
    CHECK(simulation::rate_park(high).total == simulation::kMaxRating);

    RatingInput low;
    low.old_litter = 200;
    CHECK(simulation::rate_park(low).total == 0);
}

TEST_CASE("the inputs are gathered from the park's guests, rides and litter")
{
    save::PeopleModel people;
    people.guests.resize(3);
    people.guests[0].in_park = false;
    people.guests[0].mood_now.happiness = 0.9f;
    people.guests[1].mood_now.happiness = 0.6f;
    people.guests[2].mood_now.happiness = 0.5f; // Not more happy than not
    people.litter.resize(3);
    people.litter[0].create_time = 10;
    people.litter[1].create_time = 10;
    people.litter[1].flags = 1u << 5; // Kept out of the rating
    people.litter[2].create_time = 100;

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
    CHECK(in.happy_guests == 1);
    CHECK(in.old_litter == 1); // Only the old, counted piece
    REQUIRE(in.rides.size() == 2);
    CHECK(in.rides[0].downtime_percent == doctest::Approx(10.0f));
    CHECK(in.rides[0].excitement == 3.5f);
    CHECK(in.rides[0].intensity == 4.5f);
    CHECK(in.rides[1].downtime_percent == 65.625f);
    CHECK_FALSE(in.rides[1].excitement);
}

TEST_CASE("the penalty and bonus fade each week, never below nothing")
{
    CHECK(simulation::fade({ .penalty = 100, .bonus = 10 }, false).penalty == 51);
    CHECK(simulation::fade({ .penalty = 100, .bonus = 10 }, false).bonus == 7);
    CHECK(simulation::fade({ .penalty = 100, .bonus = 2 }, true).penalty == 0);
    CHECK(simulation::fade({ .penalty = 100, .bonus = 2 }, true).bonus == 0);
}
