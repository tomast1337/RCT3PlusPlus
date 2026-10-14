// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/sky_look.hpp"

#include "park/sky_mesh.hpp"
#include "simulation/day_clock.hpp"

#include <array>
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

TEST_CASE("the sky shows the middle of the day while the sun is high")
{
    CHECK(sky_column(hours(12)) == doctest::Approx(kMiddayColumn));
    CHECK(sky_column(hours(10)) == doctest::Approx(kMiddayColumn));
    CHECK(sky_darkness(hours(12)) == 0);
    CHECK(sky_darkness(hours(9)) == 0);
}

TEST_CASE("mornings walk from night to midday, evenings from midday to night")
{
    float last = sky_column(hours(2));
    CHECK(last == doctest::Approx(kNightColumn));
    for (float h = 2.25f; h < 12; h += 0.25f)
    {
        CHECK(sky_column(hours(h)) >= last);
        last = sky_column(hours(h));
    }
    last = sky_column(hours(12));
    for (float h = 12.25f; h < 24; h += 0.25f)
    {
        CHECK(sky_column(hours(h)) >= last);
        last = sky_column(hours(h));
    }
    CHECK(last == doctest::Approx(kLateDuskColumn));
    // Dawn and sunset come when the sun is on the horizon.
    CHECK(sky_column(hours(5.4f)) > kNightColumn);
    CHECK(sky_column(hours(5.4f)) < kMorningColumn);
    CHECK(sky_column(hours(19)) > kSunsetColumn);
    CHECK(sky_column(hours(19)) < kLateDuskColumn);
}

TEST_CASE("the stars come out as the sun goes down")
{
    CHECK(sky_darkness(hours(0)) == 1);
    CHECK(sky_darkness(hours(23)) == 1);
    const float dusk = sky_darkness(hours(19.2f));
    CHECK(dusk > 0);
    CHECK(dusk < 1);
    CHECK(sky_darkness(hours(19.6f)) > dusk);
}

TEST_CASE("a band is read at the nearest colour, round its ends")
{
    const std::array<glm::vec4, 4> band { glm::vec4(0.0f), glm::vec4(0.25f), glm::vec4(0.5f), glm::vec4(1.0f) };
    CHECK(band_colour(band, 0.1f).r == 0);
    CHECK(band_colour(band, 0.3f).r == 0.25f);
    CHECK(band_colour(band, 0.99f).r == 1);
    CHECK(band_colour(band, 1.0f).r == 0);
    CHECK(band_colour(band, -0.1f).r == 1);
    CHECK(band_colour({ }, 0.5f) == glm::vec4(0.0f));
}
