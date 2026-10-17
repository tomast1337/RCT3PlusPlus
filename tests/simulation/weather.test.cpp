// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/components.hpp"
#include "simulation/park_state.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"

#include <doctest/doctest.h>

using namespace rct3::simulation;

TEST_CASE("rain soaks a guest with nothing over them and spoils their mood, not one under an umbrella")
{
    Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    auto& r = park.world().registry;
    r.ctx().emplace<Weather>(Weather { .rain = true });
    const auto bare = park.guest({ 0, 0 }, { .happiness = 0.8f });
    const auto covered = park.guest({ 0, 0 }, { .happiness = 0.8f });
    r.emplace<Umbrella>(covered);
    park.run_seconds(180);
    CHECK(r.get<Mood>(bare).wet == doctest::Approx(1));
    CHECK(r.get<Mood>(bare).happiness < 0.8f);
    CHECK(r.get<Mood>(covered).wet == 0);
    // Out of the rain they dry off.
    r.ctx().get<Weather>().rain = false;
    park.run_seconds(300);
    CHECK(r.get<Mood>(bare).wet < 0.01f);
}

TEST_CASE("a guest getting wet goes and buys an umbrella")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto stall = park.shop("Umbrellas", Product::Umbrella, { 4, 0 }, 300);
    auto& r = park.world().registry;
    r.ctx().emplace<Weather>(Weather { .rain = true });
    const auto g = park.guest({ 0, 0 }, { .cash = 1000 });
    CHECK(park.run_until([&] { return r.all_of<Umbrella>(g); }, 60));
    CHECK(r.get<Attraction>(stall).customers == 1);
    CHECK(r.get<Wallet>(g).cash == 700);
}

TEST_CASE("in the rain an open-air ride's queue empties while a covered ride's does not")
{
    for (const float cover : { 0.0f, 1.0f })
    {
        Sandbox park;
        park.path({ 0, 0 }, { 3, 0 });
        const auto ride = park.flat_ride("Twister", { 3, 0 }, 0, { .capacity = 1, .cycle_seconds = 600, .cover = cover });
        for (int i = 0; i < 6; ++i)
            park.guest({ 3, 0 });
        auto& r = park.world().registry;
        CHECK(park.run_until([&] { return r.get<CycleRide>(ride).queue.size() >= 3; }, 120));
        r.ctx().emplace<Weather>(Weather { .rain = true });
        park.run_seconds(300);
        CHECK(r.get<CycleRide>(ride).queue.empty() == (cover == 0));
    }
}

TEST_CASE("guests get thirsty faster on a hot day")
{
    float thirst[2] { };
    for (const int hot : { 0, 1 })
    {
        Sandbox park;
        park.path({ 0, 0 }, { 3, 0 });
        auto& r = park.world().registry;
        r.ctx().emplace<Weather>(Weather { .temperature = hot ? 35.0f : 20.0f });
        const auto g = park.guest({ 0, 0 });
        park.run_seconds(60);
        thirst[hot] = r.get<Needs>(g).thirst;
    }
    CHECK(thirst[1] == doctest::Approx(thirst[0] * 2));
}

TEST_CASE("the park's weather tells the guests whether it rains")
{
    ParkState state;
    state.weather = rct3::save::WeatherType::Thunderstorm;
    state.temperature = 12;
    const Weather w = guest_weather(state);
    CHECK(w.rain);
    CHECK(w.storm);
    CHECK(w.temperature == 12);
    state.weather = rct3::save::WeatherType::Cloudy;
    CHECK_FALSE(guest_weather(state).rain);
}
