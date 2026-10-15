// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park_state.hpp"

#include <doctest/doctest.h>

using namespace rct3;

TEST_CASE("a park starts from what its save holds")
{
    save::ParkModel park;
    park.has_finance = true;
    park.finance.cash = 123456;
    park.finance.loan = 5000;
    park.has_park = true;
    park.park.guests_in_park.samples = { 10.0f, 41.6f };
    park.park.park_rating.samples = { 250.0f };
    save::WorldModel world;
    world.weather = save::Weather { };
    world.weather->type = save::WeatherType::Cloudy;
    world.weather->forecast = save::WeatherType::LightRain;
    world.weather->temperature = 19.5f;

    const simulation::ParkState s = simulation::park_state(&park, world);
    CHECK(s.cash == 123456);
    CHECK(s.loan == 5000);
    CHECK(s.guests == 42);
    REQUIRE(s.rating);
    CHECK(*s.rating == doctest::Approx(0.25f));
    CHECK(s.weather == save::WeatherType::Cloudy);
    CHECK(s.forecast == save::WeatherType::LightRain);
    CHECK(s.temperature == 19.5f);
    CHECK(s.speed == simulation::GameSpeed { });
}

TEST_CASE("unlimited money has no amount, and a missing park leaves defaults")
{
    save::ParkModel park;
    park.has_finance = true;
    park.finance.flags = save::Finance::kUnlimitedCash;
    park.finance.cash = 99;
    CHECK_FALSE(simulation::park_state(&park, { }).cash);

    const simulation::ParkState none = simulation::park_state(nullptr, { });
    CHECK_FALSE(none.cash);
    CHECK_FALSE(none.rating);
    CHECK(none.weather == save::WeatherType::Unknown);
    CHECK_FALSE(none.temperature);
}
