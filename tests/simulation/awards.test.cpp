// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/awards.hpp"

#include <doctest/doctest.h>

using namespace rct3;

namespace
{
    /// A park with enough toilets for its guests, and nothing else to speak of.
    simulation::AwardFigures toilets()
    {
        return { .guests = 256, .rating = 600, .toilets = 4 };
    }
} // namespace

TEST_CASE("awards wait for the scenario to let them begin, then come once in five months")
{
    save::Awards awards { .available_mask = ~0u, .months_before_start = 1 };
    random::RandomState dice = random::seeded({ });
    CHECK(simulation::award_month(awards, toilets(), dice).empty());
    CHECK(awards.months_before_start == 0);

    CHECK(simulation::award_month(awards, toilets(), dice) == std::vector { 11 });
    REQUIRE(awards.earned.size() == 6);
    CHECK(awards.earned.front() == std::vector { 11 });
    // Had lately, it is not given again until it has passed out of the last five months.
    for (int month = 0; month < 5; ++month)
        CHECK(simulation::award_month(awards, toilets(), dice).empty());
    CHECK(simulation::award_month(awards, toilets(), dice) == std::vector { 11 });

    // Not offered by the scenario, it is never given.
    save::Awards none { .available_mask = ~(1u << 11) };
    CHECK(simulation::award_month(none, toilets(), dice).empty());
}

TEST_CASE("an award had lately keeps its opposite away")
{
    random::RandomState dice = random::seeded({ });
    simulation::AwardFigures dear { .guests = 100, .money = true, .fee_charged = true, .entrance_fee = 5000, .fair_fee = 2000 };
    CHECK(simulation::award_earned(2, dear, 0, dice)); // Worst value
    CHECK_FALSE(simulation::award_earned(2, dear, 1u << 3, dice));
    CHECK_FALSE(simulation::award_earned(3, dear, 0, dice));
    simulation::AwardFigures cheap = dear;
    cheap.entrance_fee = 500;
    CHECK(simulation::award_earned(3, cheap, 0, dice)); // Best value
    CHECK(simulation::award_is_fine(2));
    CHECK_FALSE(simulation::award_is_fine(3));
}

TEST_CASE("litter on guests' minds makes the park untidy")
{
    random::RandomState dice = random::seeded({ });
    simulation::AwardFigures park { .guests = 160, .thoughts = { { 3, 6 }, { 15, 4 } } };
    CHECK(simulation::award_earned(1, park, 0, dice));
    park.thoughts = { { 3, 6 } };
    CHECK_FALSE(simulation::award_earned(1, park, 0, dice));
}

TEST_CASE("a big park with reliable rides that never crashed is the safest")
{
    random::RandomState dice = random::seeded({ });
    simulation::AwardFigures park { .guests = 1000, .downtime = std::vector<float>(20, 0.0f), .reliability = 95 };
    CHECK(simulation::award_earned(9, park, 0, dice));
    CHECK_FALSE(simulation::award_earned(9, park, 1u << 23, dice)); // Fined for danger lately
    park.crashed = 1;
    CHECK_FALSE(simulation::award_earned(9, park, 0, dice));
    park.crashed = 0;
    park.thoughts = { { 14, 30 } }; // Many guests feeling sick
    CHECK_FALSE(simulation::award_earned(9, park, 0, dice));
}

TEST_CASE("best reliability needs many rides running and none out of action long")
{
    random::RandomState dice = random::seeded({ });
    simulation::AwardFigures park { .downtime = std::vector<float>(30, 2.0f), .reliability = 90, .running = 30 };
    CHECK(simulation::award_earned(14, park, 0, dice));
    CHECK_FALSE(simulation::award_earned(14, park, 1u << 15, dice));
    park.downtime.back() = 50;
    CHECK_FALSE(simulation::award_earned(14, park, 0, dice));
    park.downtime.back() = 2;
    park.running = 2;
    CHECK_FALSE(simulation::award_earned(14, park, 0, dice));
}
