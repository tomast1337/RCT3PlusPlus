// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/sun.hpp"

#include "simulation/day_clock.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    constexpr float hours(float h)
    {
        return h / simulation::kHoursPerDay;
    }
} // namespace

TEST_CASE("the sun is up by day and down by night")
{
    CHECK(sun_direction(hours(12)).y > 0.8f);
    CHECK(sun_direction(hours(0)).y < 0);
    CHECK(sun_elevation(hours(8)) > 0);
    CHECK(sun_elevation(hours(18)) > 0);
    CHECK(sun_elevation(hours(4)) < 0);
    CHECK(sun_elevation(hours(21)) < 0);
    // Highest at noon.
    CHECK(sun_elevation(hours(12)) > sun_elevation(hours(11)));
    CHECK(sun_elevation(hours(12)) > sun_elevation(hours(13)));
    CHECK(glm::length(sun_direction(hours(7.3f))) == doctest::Approx(1));
}

TEST_CASE("the sun rises in the east, stands south at noon and sets in the west")
{
    CHECK(sun_direction(hours(7)).x > 0);
    CHECK(sun_direction(hours(17)).x < 0);
    CHECK(sun_direction(hours(12)).x == doctest::Approx(0).epsilon(1e-5));
    CHECK(sun_direction(hours(12)).z < 0);
    // Morning and afternoon mirror each other round noon.
    CHECK(sun_elevation(hours(9)) == doctest::Approx(sun_elevation(hours(15))));
}
