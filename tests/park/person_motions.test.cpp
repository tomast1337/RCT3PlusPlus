// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/person_motions.hpp"

#include <doctest/doctest.h>

using namespace rct3;

TEST_CASE("motions are found by name whatever its case")
{
    CHECK(park::motion_number("Walk") == park::motion::kWalk);
    CHECK(park::motion_number("IDLESTANDARD") == park::motion::kIdle);
    CHECK_FALSE(park::motion_number("not a motion").has_value());
    CHECK_FALSE(park::motion_number("").has_value());
}

TEST_CASE("two spellings of one motion share its number")
{
    CHECK(park::motion_number("idle1") == park::motion_number("Idle01"));
}

TEST_CASE("walking repeats, the start of a cheer plays once")
{
    CHECK(park::motion_repeats(park::motion::kWalk));
    const auto start = park::motion_number("applausein");
    REQUIRE(start);
    CHECK_FALSE(park::motion_repeats(*start));
    CHECK(park::motion_repeats(-1));
}
