// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/weather_look.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;
using save::WeatherType;

TEST_CASE("rain and gloom ease in over a few seconds rather than jump")
{
    WeatherLook look;
    auto dice = random::seeded({ 2 });
    advance_weather_look(look, WeatherType::HeavyRain, 0.1f, dice);
    CHECK(look.rain > 0);
    CHECK(look.rain < 0.1f);
    for (int i = 0; i < 300; ++i)
        advance_weather_look(look, WeatherType::HeavyRain, 0.1f, dice);
    CHECK(look.rain == weather_aim(WeatherType::HeavyRain).rain);
    CHECK(look.gloom == weather_aim(WeatherType::HeavyRain).gloom);
    CHECK(look.flash == 0); // No lightning without a storm
    for (int i = 0; i < 300; ++i)
        advance_weather_look(look, WeatherType::Sunny, 0.1f, dice);
    CHECK(look.rain == 0);
    CHECK(look.gloom == 0);
}

TEST_CASE("a storm flashes now and then, each stroke heard a little after")
{
    WeatherLook look;
    auto dice = random::seeded({ 4 });
    int strokes = 0;
    int thunder = 0;
    float last = 0;
    for (int i = 0; i < 20 * 60; ++i) // A minute
    {
        if (advance_weather_look(look, WeatherType::Thunderstorm, 0.05f, dice))
            ++thunder;
        if (look.flash == 1 && last < 1)
            ++strokes;
        last = look.flash;
    }
    CHECK(strokes >= 3);
    CHECK(strokes <= 15);
    CHECK(thunder >= strokes - 1);
    CHECK(thunder <= strokes);
}

TEST_CASE("the gloom greys and dims light, the sun most, and a flash brightens it")
{
    const glm::vec3 warm { 1.0f, 0.8f, 0.5f };
    CHECK(weathered(warm, { }, 1) == warm);
    const WeatherLook dark { .gloom = 1 };
    const glm::vec3 sun = weathered(warm, dark, 1);
    const glm::vec3 sky = weathered(warm, dark, 0.5f);
    CHECK(sun.r < sky.r);
    CHECK(sky.r - sky.b < warm.r - warm.b); // Greyer
    const WeatherLook flash { .gloom = 1, .flash = 1 };
    CHECK(weathered(warm, flash, 1).r > sun.r);
}
