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
    save::model::Awards awards { .available_mask = ~0u, .months_before_start = 1 };
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
    save::model::Awards none { .available_mask = ~(1u << 11) };
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
    cheap.entrance_fee = units::Money { 500 };
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

TEST_CASE("dangerous happenings bring fines, coasters weighing more than pools")
{
    random::RandomState dice = random::seeded({ });
    simulation::AwardFigures park { };
    CHECK_FALSE(simulation::award_earned(23, park, 0, dice));
    park.coaster_dangers = 7;
    CHECK(simulation::award_earned(23, park, 0, dice));
    CHECK_FALSE(simulation::award_earned(24, park, 0, dice));
    CHECK_FALSE(simulation::award_earned(25, park, 0, dice));
    park.pool_dangers = 40;
    CHECK(simulation::award_earned(24, park, 0, dice));
    CHECK(simulation::award_earned(25, park, 0, dice));
    CHECK(simulation::award_is_fine(25));
}

TEST_CASE("a ride or stall carries a theme when enough of its scenery stands round it")
{
    const std::vector<park::BuildItem> catalogue { { .symbol = "Grave", .overlay = "Style/Themed/Gloom/Style", .width = 4, .length = 4, .style_index = 9, .style = "Gloom" } };
    save::model::RideModel rides;
    for (const std::int32_t col : { 10, 90 })
        rides.stalls.push_back({ .base = { .id = static_cast<save::model::ObjectId>(col), .overlay_path = "Style\\Themed\\Gloom\\Style", .spot = save::model::PlacementSpot { .col = col, .row = 10 } } });
    std::vector<save::model::PlacedObject> objects;
    for (std::int32_t i = 0; i < 6; ++i)
        objects.push_back({ .id = 100 + static_cast<std::uint64_t>(i), .symbol = "Grave:svd", .overlay = "Style\\Themed\\Gloom\\Style", .col = 12 + i % 2, .row = 8 + i });
    // Only the stall among the graves carries the theme.
    CHECK(simulation::themed_attractions(rides, objects, catalogue) == std::map<std::string, std::int32_t, std::less<>> { { "Gloom", 1 } });
    objects.pop_back();
    CHECK(simulation::themed_attractions(rides, objects, catalogue).empty());

    random::RandomState dice = random::seeded({ });
    simulation::AwardFigures park { .themed = { { "Spooky", 9 } } };
    CHECK(simulation::award_earned(17, park, 0, dice));
    CHECK_FALSE(simulation::award_earned(16, park, 0, dice));
}

TEST_CASE("healthy rare animals win best animals, and sickly ones count hard against it")
{
    random::RandomState dice = random::seeded({ });
    simulation::AwardFigures zoo;
    // Thirteen endangered animals in full health make 13; fewer do not pass 12.
    zoo.animals.assign(13, { .rarity = 1, .health = 1 });
    CHECK(simulation::award_earned(28, zoo, 0, dice));
    zoo.animals.resize(12);
    CHECK_FALSE(simulation::award_earned(28, zoo, 0, dice));
    // One at a quarter health takes away what five healthy ones give.
    zoo.animals.assign(16, { .rarity = 1, .health = 1 });
    CHECK(simulation::award_earned(28, zoo, 0, dice));
    zoo.animals.push_back({ .rarity = 1, .health = 0.25f });
    CHECK_FALSE(simulation::award_earned(28, zoo, 0, dice));
    // Common animals count for nothing.
    zoo.animals.assign(40, { .rarity = 0, .health = 1 });
    CHECK_FALSE(simulation::award_earned(28, zoo, 0, dice));
}

TEST_CASE("the sum for best animals drops its fraction as each animal is added")
{
    random::RandomState dice = random::seeded({ });
    // Each adds 0.9: kept whole, nothing builds up, though the true sum would pass 12.
    simulation::AwardFigures zoo;
    zoo.animals.assign(20, { .rarity = 0.9f, .health = 1 });
    CHECK_FALSE(simulation::award_earned(28, zoo, 0, dice));
}

TEST_CASE("young healthy rare animals are the cutest")
{
    random::RandomState dice = random::seeded({ });
    simulation::AwardFigures zoo;
    zoo.animals.assign(11, { .rarity = 1, .health = 1, .young = true });
    CHECK(simulation::award_earned(29, zoo, 0, dice));
    // Adults are not cute.
    for (auto& a : zoo.animals)
        a.young = false;
    CHECK_FALSE(simulation::award_earned(29, zoo, 0, dice));
    // A quarter rarity halves an animal's part: 21 such young ones make 10.5.
    zoo.animals.assign(21, { .rarity = 0.25f, .health = 1, .young = true });
    CHECK(simulation::award_earned(29, zoo, 0, dice));
    zoo.animals.resize(20);
    CHECK_FALSE(simulation::award_earned(29, zoo, 0, dice));
}

TEST_CASE("more than four poorly animals bring the fine for neglect")
{
    random::RandomState dice = random::seeded({ });
    simulation::AwardFigures zoo;
    zoo.animals.assign(5, { .rarity = 0.5f, .health = 0.3f });
    CHECK(simulation::award_earned(30, zoo, 0, dice));
    CHECK(simulation::award_is_fine(30));
    zoo.animals.back().health = 0.4f;
    CHECK_FALSE(simulation::award_earned(30, zoo, 0, dice));
}
