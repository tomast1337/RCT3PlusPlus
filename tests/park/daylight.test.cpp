// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/daylight.hpp"

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

    float brightness(glm::vec3 c)
    {
        return (c.r + c.g + c.b) / 3;
    }
} // namespace

TEST_CASE("by day the sun and the open sky light the park")
{
    const Daylight noon = daylight(hours(12));
    CHECK(noon.sun_direction == sun_direction(hours(12)));
    CHECK(noon.sun.r == doctest::Approx(kHighSun.r));
    CHECK(noon.sky.b == doctest::Approx(kDaySky.b));
    CHECK(brightness(noon.ground) > 0);
    CHECK(brightness(noon.ground) < brightness(noon.sky));
    // Flat ground at noon ends up about as bright as its picture.
    const float flat = brightness(noon.sky) + brightness(noon.sun) * noon.sun_direction.y;
    CHECK(flat > 0.85f);
    CHECK(flat < 1.15f);
}

TEST_CASE("by night only a dim blue sky is left")
{
    const Daylight night = daylight(hours(0));
    CHECK(night.sun == glm::vec3(0.0f));
    CHECK(night.sky.b == doctest::Approx(kNightSky.b));
    CHECK(night.sky.b > night.sky.r);
}

TEST_CASE("the sun goes orange and fades as it sets, and the sky darkens after it")
{
    const Daylight afternoon = daylight(hours(16));
    const Daylight low = daylight(hours(18.6f));
    CHECK(low.sun.b / low.sun.r < afternoon.sun.b / afternoon.sun.r);
    CHECK(brightness(low.sun) < brightness(afternoon.sun));
    const Daylight set = daylight(hours(19.2f));
    CHECK(brightness(set.sun) < brightness(low.sun));
    CHECK(brightness(set.sky) > brightness(daylight(hours(0)).sky));
    CHECK(brightness(set.sky) < brightness(afternoon.sky));
}

TEST_CASE("two lights mix, the sun's direction turning between them")
{
    const Daylight a = daylight(hours(9));
    const Daylight b = daylight(hours(15));
    CHECK(mixed(a, b, 0).sun == a.sun);
    CHECK(mixed(a, b, 1).sky == b.sky);
    const Daylight half = mixed(a, b, 0.5f);
    CHECK(glm::length(half.sun_direction) == doctest::Approx(1));
    CHECK(half.sun_direction.x == doctest::Approx(0).epsilon(1e-4));
    CHECK(half.sky.g == doctest::Approx((a.sky.g + b.sky.g) / 2));
}
