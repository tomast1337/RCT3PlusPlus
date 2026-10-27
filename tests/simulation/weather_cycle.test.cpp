// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/weather_cycle.hpp"

#include "simulation/park_state.hpp"

#include <doctest/doctest.h>
#include <numeric>

using namespace rct3;
using namespace rct3::simulation;

namespace
{
    constexpr float kHour = 24; // Calendar units in a game hour, about

    /// Kinds of weather seen over `months` months of `climate`, by save number.
    std::array<int, kRolledWeathers> season(Climate climate, int months)
    {
        ParkState s;
        s.climate = static_cast<std::int32_t>(climate);
        auto dice = random::seeded({ 7 });
        std::array<int, kRolledWeathers> seen { };
        for (int m = 0; m < months; ++m)
        {
            s.date.month = m % 8;
            for (int h = 0; h < 15 * 24; ++h) // About a month of game hours
            {
                advance_weather(s, kHour, dice, false);
                ++seen.at(static_cast<std::size_t>(s.weather));
            }
        }
        return seen;
    }
} // namespace

TEST_CASE("every climate's odds add up to one in every month")
{
    for (std::int32_t c = 0; c < static_cast<std::int32_t>(Climate::Count_); ++c)
        for (std::int32_t m = 0; m < 8; ++m)
        {
            const auto odds = climate_month(c, m).odds;
            CHECK(std::accumulate(odds.begin(), odds.end(), 0.0) == doctest::Approx(1.0));
        }
    // An unknown climate is temperate, and months wrap.
    CHECK(climate_month(99, 0).warmth == climate_month(0, 0).warmth);
    CHECK(climate_month(0, 9).warmth == climate_month(0, 1).warmth);
}

TEST_CASE("a dry climate never rains and a wet one does")
{
    const auto mars = season(Climate::Martian, 8);
    CHECK(mars[3] + mars[4] + mars[5] == 0);
    const auto jungle = season(Climate::Jungle, 8);
    CHECK(jungle[3] + jungle[4] > 0);
    CHECK(jungle[5] > 0);
}

TEST_CASE("the forecast arrives when the weather's hold runs out, and a new one is rolled")
{
    ParkState s;
    s.weather = save::WeatherType::Sunny;
    s.forecast = save::WeatherType::HeavyRain;
    s.weather_hold = 10;
    s.temperature = 20;
    auto dice = random::seeded({ 3 });
    advance_weather(s, 5, dice, false);
    CHECK(s.weather == save::WeatherType::Sunny);
    CHECK(s.weather_hold == doctest::Approx(5));
    advance_weather(s, 6, dice, false);
    CHECK(s.weather == save::WeatherType::HeavyRain);
    CHECK(s.forecast);
    CHECK(s.weather_hold > 0);
}

TEST_CASE("the temperature eases towards what the weather and month bring")
{
    ParkState s;
    s.climate = static_cast<std::int32_t>(Climate::Desert);
    s.weather = save::WeatherType::Sunny;
    s.forecast = save::WeatherType::Sunny;
    s.weather_hold = 1e6f;
    s.temperature = 0;
    auto dice = random::seeded({ 1 });
    const float goal = weather_temperature(s.climate, 0, save::WeatherType::Sunny);
    advance_weather(s, 1, dice, false);
    CHECK(*s.temperature > 0);
    CHECK(*s.temperature < goal);
    for (int i = 0; i < 1000; ++i)
        advance_weather(s, 1, dice, false);
    CHECK(*s.temperature == doctest::Approx(goal));
    CHECK(s.wind >= 0);
    CHECK(s.wind <= 1);
}

TEST_CASE("frozen weather stays as it is")
{
    ParkState s;
    s.weather = save::WeatherType::LightRain;
    s.forecast = save::WeatherType::Sunny;
    s.weather_hold = 1;
    s.temperature = 14;
    s.wind = 0.5f;
    auto dice = random::seeded({ 5 });
    advance_weather(s, 1000, dice, true);
    CHECK(s.weather == save::WeatherType::LightRain);
    CHECK(s.forecast == save::WeatherType::Sunny);
    CHECK(*s.temperature == 14);
    CHECK(s.wind == 0.5f);
}
