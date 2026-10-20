// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/random_events.hpp"

#include <array>
#include <cmath>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation;

TEST_CASE("nothing happens until a rate is set, and nothing is drawn for it")
{
    random::RandomState s = random::seeded(random::Seed { 1 });
    const random::RandomState before = s;
    CHECK(roll_events(s, { }, 1000).empty());
    CHECK(s == before);
}

TEST_CASE("an event happens about as often as its rate says")
{
    EventRates rates;
    rates[RandomEvent::RideBreakdown] = 2; // Twice an hour
    random::RandomState s = random::seeded(random::Seed { 5 });
    int happened = 0;
    for (int i = 0; i < 10000; ++i)
        happened += roll_events(s, rates, 0.1).empty() ? 0 : 1;
    const double expected = 10000 * chance_in(2, 0.1);
    CHECK(std::abs(happened - expected) < 0.05 * expected);
    CHECK(chance_in(2, 0.1) == doctest::Approx(1 - std::exp(-0.2)));
    CHECK(chance_in(0, 1) == 0);
    CHECK(chance_in(1, -1) == 0);
}

TEST_CASE("events come in their order, each at most once")
{
    EventRates rates;
    rates[RandomEvent::AnimalEscape] = 1e9;
    rates[RandomEvent::WeatherChange] = 1e9;
    random::RandomState s = random::seeded(random::Seed { 3 });
    CHECK(roll_events(s, rates, 1) == std::vector<RandomEvent> { RandomEvent::WeatherChange, RandomEvent::AnimalEscape });
    CHECK(event_name(RandomEvent::RideBreakdown) == "ride_breakdown");
}

TEST_CASE("a weighted pick follows the weights and skips the empty ones")
{
    random::RandomState s = random::seeded(random::Seed { 11 });
    const std::array<double, 4> weights { 0, 1, -5, 3 };
    std::array<int, 4> picked { };
    for (int i = 0; i < 4000; ++i)
    {
        const auto p = pick_weighted(s, weights);
        REQUIRE(p);
        ++picked[*p];
    }
    CHECK(picked[0] == 0);
    CHECK(picked[2] == 0);
    CHECK(picked[3] > 2 * picked[1]);
    CHECK_FALSE(pick_weighted(s, std::array<double, 2> { 0, -1 }));
    CHECK_FALSE(pick_weighted(s, { }));
}
