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

TEST_CASE("in the rain a guest with no umbrella walks to cover and stays there; one with an umbrella walks on")
{
    Sandbox park;
    park.path({ 0, 0 }, { 6, 0 });
    auto& paths = park.world().paths;
    const NodeId shelter = *paths.find({ 6, 0, 0 });
    paths.cover(shelter, true, false);
    auto& r = park.world().registry;
    r.ctx().emplace<Weather>(Weather { .rain = true });
    const auto bare = park.guest({ 0, 0 });
    const auto dry = park.guest({ 0, 0 });
    r.emplace<Umbrella>(dry);
    REQUIRE(park.run_until([&] { return r.get<Stride>(bare).from == shelter; }, 60));
    park.run_seconds(60);
    CHECK(r.get<Stride>(bare).from == shelter);
    CHECK(r.get<Stride>(bare).to == shelter);
    // Under the roof they dry off.
    CHECK(r.get<Mood>(bare).wet < 0.2f);
    CHECK(r.get<Mood>(dry).wet == 0);
}

TEST_CASE("guests queueing under cover stay in an open-air ride's queue in the rain")
{
    Sandbox park;
    park.path({ 0, 0 }, { 2, 0 }).queue({ 3, 0 }, { 6, 0 });
    const auto ride = park.flat_ride("Spinner", { 6, 0 }, 0, { .capacity = 1, .cycle_seconds = 600, .line = { { 6, 0 }, { 5, 0 }, { 4, 0 }, { 3, 0 } } });
    auto& paths = park.world().paths;
    for (int col = 3; col <= 6; ++col)
        paths.cover(*paths.find({ col, 0, 0 }), true, false);
    auto& r = park.world().registry;
    for (int i = 0; i < 4; ++i)
        r.get<Errand>(park.guest({ 0, 0 })) = { ErrandKind::Heading, ride, 0 };
    REQUIRE(park.run_until([&] { return r.get<CycleRide>(ride).queue.size() == 3; }, 60));
    r.ctx().emplace<Weather>(Weather { .rain = true, .storm = true });
    park.run_seconds(300);
    CHECK(r.get<CycleRide>(ride).queue.size() == 3);
}

TEST_CASE("in a storm guests of any age head home, by day too")
{
    Sandbox park;
    park.path({ 0, 0 }, { 5, 0 });
    auto& r = park.world().registry;
    r.emplace<ParkGate>(r.create(), ParkGate { park.world().paths.find({ 0, 0, 0 }).value() });
    const auto teen = park.guest({ 5, 0 });
    r.get<Visitor>(teen).age = AgeGroup::Teen;
    park.run_seconds(300);
    CHECK_FALSE(r.all_of<Left>(teen)); // Not in fair weather
    r.ctx().emplace<Weather>(Weather { .rain = true, .storm = true });
    CHECK(park.run_until([&] { return r.all_of<Left>(teen); }, 3600));
}

TEST_CASE("fewer guests come in the rain and the cold, more on a sunny day")
{
    const double mild = arrivals_per_second({ });
    CHECK(arrivals_per_second({ .rain = true }) < mild);
    CHECK(arrivals_per_second({ .rain = true, .storm = true }) < arrivals_per_second({ .rain = true }));
    CHECK(arrivals_per_second({ .temperature = 2 }) < mild);
    CHECK(arrivals_per_second({ .sunny = true }) > mild);
}

TEST_CASE("on a hot day a thirsty guest goes for a cold drink over a nearer plain one, and it pleases them more")
{
    Sandbox park;
    park.path({ 0, 0 }, { 8, 0 });
    const auto plain = park.shop("Soda", Product::Drink, { 2, 0 }, 100);
    const auto cold = park.shop("Milkshakes", Product::Drink, { 8, 0 }, 100);
    auto& r = park.world().registry;
    r.get<Shop>(cold).serve = Serve::Cold;
    r.ctx().emplace<Weather>(Weather { .temperature = 30 });
    const auto g = park.guest({ 0, 0 }, { .thirst = 0.9f, .happiness = 0.5f });
    REQUIRE(park.run_until([&] { return r.get<Attraction>(cold).customers == 1; }, 60));
    CHECK(r.get<Attraction>(plain).customers == 0);
    CHECK(r.get<Mood>(g).happiness > 0.5f + 0.05f);
}

TEST_CASE("the sun heats guests out in it, not those in the shade, and a cold treat cools them")
{
    Sandbox park;
    park.path({ 0, 0 }, { 0, 0 }).path({ 4, 0 }, { 4, 0 });
    auto& paths = park.world().paths;
    paths.cover(*paths.find({ 4, 0, 0 }), false, true);
    auto& r = park.world().registry;
    r.ctx().emplace<Weather>(Weather { .sunny = true, .temperature = 32 });
    const auto out = park.guest({ 0, 0 });
    const auto shaded = park.guest({ 4, 0 });
    park.run_seconds(600);
    CHECK(r.get<Mood>(out).heat > 0.55f);
    CHECK(r.get<Mood>(shaded).heat == 0);
    // Cloud over the sun, and they cool down.
    r.ctx().get<Weather>().sunny = false;
    park.run_seconds(120);
    CHECK(r.get<Mood>(out).heat == 0);
}

TEST_CASE("a guest too hot buys an ice cream though not hungry, and it cools them")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    const auto stand = park.shop("Ice Cream", Product::Food, { 4, 0 }, 100);
    auto& r = park.world().registry;
    r.get<Shop>(stand).serve = Serve::Cold;
    r.ctx().emplace<Weather>(Weather { .sunny = true, .temperature = 32 });
    const auto g = park.guest({ 0, 0 });
    r.get<Mood>(g).heat = 0.9f;
    REQUIRE(park.run_until([&] { return r.get<Attraction>(stand).customers == 1; }, 60));
    CHECK(r.get<Mood>(g).heat < 0.1f);
}

TEST_CASE("a splashing ride leaves its riders damp and cool")
{
    Sandbox park;
    park.path({ 0, 0 }, { 3, 0 });
    const auto ride = park.flat_ride("Log Flume", { 3, 0 }, 0, { .capacity = 1, .cycle_seconds = 10 });
    auto& r = park.world().registry;
    r.get<CycleRide>(ride).splash = true;
    const auto g = park.guest({ 0, 0 });
    r.get<Mood>(g).heat = 0.8f;
    r.get<Errand>(g) = { ErrandKind::Heading, ride, 0 };
    REQUIRE(park.run_until([&] { return r.get<Attraction>(ride).customers == 1 && r.get<Errand>(g).kind != ErrandKind::Riding; }, 60));
    CHECK(r.get<Mood>(g).wet > 0.2f);
    CHECK(r.get<Mood>(g).heat == 0);
}

TEST_CASE("a snowstorm keeps guests in as a thunderstorm does; sun is told apart from cloud")
{
    ParkState state;
    state.weather = rct3::save::WeatherType::Snowstorm;
    CHECK(guest_weather(state).storm);
    state.weather = rct3::save::WeatherType::Sunny;
    CHECK(guest_weather(state).sunny);
    state.weather = rct3::save::WeatherType::Cloudy;
    CHECK_FALSE(guest_weather(state).sunny);
}
