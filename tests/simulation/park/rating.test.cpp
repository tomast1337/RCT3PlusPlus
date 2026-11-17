// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park/rating.hpp"

#include "simulation/awards.hpp"
#include "simulation/tuning.hpp"

#include <cmath>
#include <doctest/doctest.h>

using namespace rct3;
using simulation::park::RatingInput;
using simulation::park::RatingParts;
using simulation::park::RideRatingInput;

TEST_CASE("an empty park scores half its happiness and nothing else")
{
    const RatingParts p = simulation::park::rate_park({ });
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
    const RatingParts p = simulation::park::rate_park(in);
    CHECK(p.happiness == 375);
    CHECK(p.rides == 146);      // A ride scoring in full, times the variety of two rides
    CHECK(p.reliability == 75); // Running three quarters of the time on average
    CHECK(p.guests == 275);     // Most of the part at 750 guests
    CHECK(p.litter == -50);
    CHECK(p.harder == 0);
    CHECK(p.total == 375 + 146 + 75 + 275 - 50);
}

TEST_CASE("a ride too intense for comfort scores less, and nothing past the range")
{
    RatingInput mild;
    mild.rides = { { .downtime_percent = 0, .excitement = 7.0f, .intensity = 7.0f } };
    RatingInput harsh = mild;
    harsh.rides[0].intensity = 9.0f;
    RatingInput brutal = mild;
    brutal.rides[0].intensity = 11.0f;
    CHECK(simulation::park::rate_park(harsh).rides * 2 == doctest::Approx(simulation::park::rate_park(mild).rides).epsilon(0.05));
    CHECK(simulation::park::rate_park(brutal).rides == 0);
}

TEST_CASE("the harder rating takes a share off, and the total stays in range")
{
    RatingInput in;
    in.harder = true;
    const RatingParts p = simulation::park::rate_park(in);
    CHECK(p.harder == -38);
    CHECK(p.total == 212);

    RatingInput high;
    high.guests = 100000;
    high.guests_inside = 1;
    high.happiness = 1;
    high.rides.assign(100, { .downtime_percent = 0, .excitement = 9.0f, .intensity = 6.0f });
    CHECK(simulation::park::rate_park(high).total == 1000);

    RatingInput low;
    low.old_litter = 1000;
    low.guests_inside = 1;
    CHECK(simulation::park::rate_park(low).litter == -600);
    CHECK(simulation::park::rate_park(low).total == 0);
}

TEST_CASE("the inputs are gathered from the park's guests, rides and litter")
{
    save::model::PeopleModel people;
    people.guests.resize(3);
    people.guests[0].in_park = false;
    people.guests[0].mood_now.happiness = 0.9f;
    people.guests[1].mood_now.happiness = 0.6f;
    people.guests[2].mood_now.happiness = 0.5f;
    people.litter.resize(2);
    people.litter[0].create_time = 10;
    people.litter[1].create_time = 300; // Too new to count

    save::model::RideModel rides;
    rides.rides.resize(3);
    rides.rides[2].kind = save::model::RideKind::Special; // Facilities are not rated
    rides.rides[0].breakdown.downtime_windows = { 16, 0, 0, 0, 0, 0, 0, 0 };
    rides.rides[0].assessment.valid = true;
    rides.rides[0].assessment.excitement = 3.5f;
    rides.rides[0].assessment.intensity = 4.5f;
    rides.rides[1].kind = save::model::RideKind::Flat;
    rides.rides[1].breakdown.saved_downtime_percent = 65.625f; // Wins over the windows

    const RatingInput in = simulation::park::rating_input(people, rides, { }, 300);
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
    CHECK(simulation::park::next_rating(std::nullopt, 600) == doctest::Approx(0.6f));
    CHECK(simulation::park::next_rating(0.2f, 600) == doctest::Approx(0.4f));
    CHECK(simulation::park::next_rating(0.8f, 2000) == doctest::Approx(0.9f));
}

TEST_CASE("lost guests take some off each, up to a cap")
{
    const auto& t = simulation::kTuning.rating;
    RatingInput in;
    in.lost_guests = 2;
    CHECK(simulation::park::rate_park(in).lost_guests == -2 * t.per_lost_guest);
    in.lost_guests = 100000;
    CHECK(simulation::park::rate_park(in).lost_guests == -t.most_lost_guests);
}

TEST_CASE("a crash takes most off just after, less as the park forgets it")
{
    const float crash = simulation::kTuning.rating.crash.value;
    const float forgotten = simulation::kTuning.crashes.forgotten.value;
    RatingInput in;
    CHECK(simulation::park::rate_park(in).crash == 0);
    in.since_crash = units::Seconds { 0 };
    CHECK(simulation::park::rate_park(in).crash == -static_cast<std::int32_t>(std::lround(crash)));
    in.since_crash = units::Seconds { forgotten / 2 };
    CHECK(simulation::park::rate_park(in).crash == -static_cast<std::int32_t>(std::lround(crash / 2)));
    in.since_crash = units::Seconds { forgotten };
    CHECK(simulation::park::rate_park(in).crash == 0);
}

TEST_CASE("awards add, fines take off, and a prize adds what is left of it untouched by the harder rating")
{
    const auto& t = simulation::kTuning.rating;
    RatingInput in;
    in.awards = 2;
    in.fines = 1;
    in.prize = 40;
    in.harder = true;
    const RatingParts p = simulation::park::rate_park(in);
    CHECK(p.awards == static_cast<std::int32_t>(std::lround(2 * t.per_award.value - t.per_fine.value)));
    CHECK(p.prize == 40);
    const std::int32_t sum = p.happiness + p.awards;
    CHECK(p.harder == -static_cast<std::int32_t>(std::lround(static_cast<float>(sum) * t.harder_share.value)));
    CHECK(p.total == sum + p.harder + 40);
}

TEST_CASE("lost guests, crashes and awards are gathered from the park")
{
    save::model::PeopleModel people;
    people.guests.resize(3);
    people.guests[0].person.state = save::model::PersonState::Lost;
    people.guests[1].person.state = save::model::PersonState::Lost;
    people.guests[1].in_park = false; // Gone, so not counted

    save::model::RideModel rides;
    rides.rides.resize(3);
    rides.rides[0].base.time_since_crash = units::Seconds { 600 };
    rides.rides[1].base.time_since_crash = units::Seconds { 200 }; // The latest
    rides.rides[2].base.time_since_crash = units::Seconds { 100 };
    rides.rides[2].base.demolished = true;

    constexpr std::int32_t kFine = 30; // Neglected animals
    REQUIRE(simulation::award_is_fine(kFine));
    std::int32_t award = 0;
    while (simulation::award_is_fine(award) || simulation::award_name(award).empty())
        ++award;
    save::model::Awards awards;
    awards.earned = { { award, kFine }, { }, { award } };

    const RatingInput in = simulation::park::rating_input(people, rides, awards, 0);
    CHECK(in.lost_guests == 1);
    REQUIRE(in.since_crash);
    CHECK(in.since_crash->value == 200);
    CHECK(in.awards == 2);
    CHECK(in.fines == 1);
}
