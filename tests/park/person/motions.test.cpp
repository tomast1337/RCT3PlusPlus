// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/person/motions.hpp"

#include <doctest/doctest.h>

using namespace rct3;

TEST_CASE("motions are found by name whatever its case")
{
    CHECK(park::person::motion_number("Walk") == park::person::motion::kWalk);
    CHECK(park::person::motion_number("IDLESTANDARD") == park::person::motion::kIdle);
    CHECK_FALSE(park::person::motion_number("not a motion").has_value());
    CHECK_FALSE(park::person::motion_number("").has_value());
}

TEST_CASE("two spellings of one motion share its number")
{
    CHECK(park::person::motion_number("idle1") == park::person::motion_number("Idle01"));
}

TEST_CASE("walking repeats, the start of a cheer plays once")
{
    CHECK(park::person::motion_repeats(park::person::motion::kWalk));
    const auto start = park::person::motion_number("applausein");
    REQUIRE(start);
    CHECK_FALSE(park::person::motion_repeats(*start));
    CHECK(park::person::motion_repeats(-1));
}

TEST_CASE("riders behind a lap bar have hands up, covered eyes, strained and waiting motions of their own")
{
    const auto bar = park::person::rider_motions(11);
    REQUIRE(bar);
    CHECK(bar->ride == park::person::motion_number("ridesitbarloop"));
    CHECK(bar->hands_up == park::person::motion_number("handsupbarloop"));
    CHECK(bar->cover_eyes == park::person::motion_number("covereyesbarloop"));
    CHECK(bar->strained == park::person::motion_number("geforcebar"));
    CHECK(bar->wait == park::person::motion_number("ridesitbarwait"));
}

TEST_CASE("riders in a plain car only ride, and an unknown seating has no motions")
{
    const auto car = park::person::rider_motions(2);
    REQUIRE(car);
    CHECK(car->hands_up == car->ride);
    CHECK(car->wait == car->ride);
    CHECK_FALSE(park::person::rider_motions(200).has_value());
}

TEST_CASE("carousel riders sit their horse without reacting, and dancers wait standing")
{
    const auto horse = park::person::rider_motions(22);
    REQUIRE(horse);
    CHECK(horse->ride == park::person::motion_number("ridehorsemerrygor"));
    CHECK(horse->hands_up == horse->ride);
    const auto dance = park::person::rider_motions(26);
    REQUIRE(dance);
    CHECK(dance->ride == park::person::motion_number("dance"));
    CHECK(dance->wait == park::person::motion::kIdle);
}

TEST_CASE("riders behind a lap bar raise and lower their hands with motions of their own")
{
    const auto bar = park::person::rider_motions(11);
    REQUIRE(bar);
    CHECK(bar->hands_up_in == park::person::motion_number("handsupbarin"));
    CHECK(bar->hands_up_out == park::person::motion_number("handsupbarout"));
    CHECK(bar->cover_eyes_in == park::person::motion_number("covereyesbarin"));
    CHECK_FALSE(park::person::motion_repeats(*bar->hands_up_in));
    CHECK_FALSE(park::person::rider_motions(2)->hands_up_in.has_value());
}

TEST_CASE("clips play in turn, each once, and the last for good")
{
    const std::array<park::person::Clip, 3> clips { { { 1, 0.5f }, { 2, 1 }, { 3, 0 } } };
    CHECK(park::person::clip_at(clips, 0.25f).motion == 1);
    CHECK(park::person::clip_at(clips, 0.25f).seconds == doctest::Approx(0.25f));
    CHECK(park::person::clip_at(clips, 1).motion == 2);
    CHECK(park::person::clip_at(clips, 1).seconds == doctest::Approx(0.5f));
    CHECK(park::person::clip_at(clips, 10).motion == 3);
    CHECK(park::person::clip_at(clips, 10).seconds == doctest::Approx(8.5f));
    const std::array<park::person::Clip, 1> loop { { { 4, 0 } } };
    CHECK(park::person::clip_at(loop, 3).motion == 4);
}
