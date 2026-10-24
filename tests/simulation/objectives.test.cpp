// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/objectives.hpp"

#include "simulation/park_state.hpp"

#include <doctest/doctest.h>
#include <utility>

using namespace rct3;

namespace
{
    constexpr float kMonth = save::GameClock::kUnitsPerMonth;

    save::Condition goal(std::int32_t level, save::ConditionKind kind, std::int64_t value, std::int32_t months = 0)
    {
        return { .kind = kind, .difficulty = level, .months_to_hold = months, .value = value };
    }
} // namespace

TEST_CASE("a level is won once all its goals are met, and only then is the next one looked at")
{
    save::ObjectiveSet set;
    set.objectives = { { .difficulty = 0, .prize_id = 2 }, { .difficulty = 1, .prize_id = 6 } };
    set.conditions = { goal(0, save::ConditionKind::ParkGuests, 300), goal(0, save::ConditionKind::ParkRating, 600, 2), goal(1, save::ConditionKind::ParkGuests, 100) };

    simulation::ParkFigures f { .guests = 500, .rating = 700 };
    CHECK(simulation::level_playing(set) == 0);
    CHECK_FALSE(simulation::check_goals(set, f, 0));
    CHECK(set.conditions[0].completed);       // Met at any time
    CHECK_FALSE(set.conditions[2].completed); // A later level waits, though it is met
    CHECK_FALSE(simulation::check_goals(set, f, 1.5f * kMonth));
    f.rating = 500; // A break starts the two months again
    CHECK_FALSE(simulation::check_goals(set, f, 1.8f * kMonth));
    f.rating = 650;
    CHECK_FALSE(simulation::check_goals(set, f, 2 * kMonth));
    CHECK_FALSE(simulation::check_goals(set, f, 3.9f * kMonth));
    f.guests = 0; // A goal met stays met
    const auto won = simulation::check_goals(set, f, 4 * kMonth);
    REQUIRE(won);
    CHECK(*won == std::array { 2, simulation::kApprenticePrize });
    CHECK(set.objectives[0].success);
    CHECK(simulation::level_playing(set) == 1);
    f.guests = 100;
    CHECK(simulation::check_goals(set, f, 4 * kMonth) == std::array { 6, simulation::kApprenticePrize + 1 });
    // No tycoon level here: nothing more to win.
    CHECK(simulation::level_playing(set) == 2);
    CHECK_FALSE(simulation::check_goals(set, f, 5 * kMonth));
}

TEST_CASE("a goal the simulation cannot judge is never met, so its level is not won")
{
    save::ObjectiveSet set;
    set.objectives = { { .difficulty = 0 } };
    set.conditions = { goal(0, save::ConditionKind::ParkGuests, 0), goal(0, save::ConditionKind::PoolGuests, 0) };
    CHECK(simulation::judged(save::ConditionKind::ParkGuests));
    CHECK_FALSE(simulation::judged(save::ConditionKind::PoolGuests));
    CHECK_FALSE(simulation::judged(save::ConditionKind::Vip));
    CHECK_FALSE(simulation::check_goals(set, { .guests = 10 }, 0));
    CHECK(set.conditions[0].completed);
    CHECK_FALSE(set.conditions[1].completed);
}

TEST_CASE("a coaster goal counts the tested coasters reaching its figure")
{
    save::Ride fast, slow, untested, wheel;
    fast.stats.last_lap = save::TestResults { .max_speed = 30, .valid = true };
    slow.stats.last_lap = save::TestResults { .max_speed = 15, .valid = true };
    wheel.stats.last_lap = save::TestResults { .max_speed = 40, .valid = true };
    const std::array rides { simulation::GoalRide { simulation::RideGroup::Coaster, &fast }, simulation::GoalRide { simulation::RideGroup::Coaster, &slow }, simulation::GoalRide { simulation::RideGroup::Coaster, &untested }, simulation::GoalRide { simulation::RideGroup::Other, &wheel } };
    const simulation::ParkFigures f { .rides = rides };
    const auto met = [&](save::CoasterTarget target) {
        save::ObjectiveSet set;
        set.objectives = { { .difficulty = 0 } };
        save::Condition c { .kind = save::ConditionKind::Coaster };
        c.coaster = target;
        set.conditions = { c };
        return simulation::check_goals(set, f, 0).has_value();
    };
    CHECK(met({ .value = 20, .flags = 7, .rides = 1 }));
    CHECK_FALSE(met({ .value = 20, .flags = 7, .rides = 2 })); // The wheel is no coaster
    CHECK(met({ .value = 20, .flags = 7 | save::CoasterTarget::kAtMost, .rides = 1 }));
    CHECK_FALSE(met({ .value = 20, .flags = 7 | save::CoasterTarget::kEveryRide, .rides = 1 }));
}

TEST_CASE("takings count the last whole month, and prizes pay out")
{
    simulation::Takings t;
    simulation::count_takings(t, 0.5f * kMonth, 1000, 50);
    simulation::count_takings(t, 1.2f * kMonth, 1400, 60);
    simulation::count_takings(t, 1.9f * kMonth, 2000, 90);
    CHECK(t.rides == 400); // The first, part counted month
    simulation::count_takings(t, 2.1f * kMonth, 2600, 100);
    CHECK(t.rides == 1200);
    CHECK(t.shops == 40);

    simulation::ParkState park;
    park.cash = 0;
    park.rating = 0.99f;
    save::Finance finance { .max_loan = 1000, .interest_rate_percent = 8 };
    save::TerrainGrid land { .width = 2, .height = 1, .cells = { { .flags = std::to_underlying(save::TileFlag::NeverPurchasable) }, { } } };
    simulation::give_prize(park, finance, land, 1);
    simulation::give_prize(park, finance, land, 2);
    CHECK(park.cash > 0);
    simulation::give_prize(park, finance, land, 4);
    CHECK(park.rating == 1.0f);
    simulation::give_prize(park, finance, land, 5);
    simulation::give_prize(park, finance, land, 6);
    CHECK(finance.interest_rate_percent == 4);
    CHECK(finance.max_loan == 2000);
    simulation::give_prize(park, finance, land, 7);
    CHECK(land.cells[0].has(save::TileFlag::ForSale));
    CHECK_FALSE(land.cells[1].has(save::TileFlag::ForSale));
    CHECK(simulation::prize_message(12) == "Prize_TycoonAward");
    CHECK(simulation::prize_name(0) == "Prize_Invalid");
}

TEST_CASE("a dated goal warns a month and a week before, is judged on its date, and a miss moves it a year on")
{
    constexpr float kWeek = save::GameClock::kUnitsPerWeek;
    constexpr float kYear = save::GameClock::kUnitsPerYear;
    save::ObjectiveSet set;
    set.objectives = { { .difficulty = 0 } };
    save::Condition dated = goal(0, save::ConditionKind::ParkGuests, 300);
    dated.next_check_time = 6 * kMonth;
    dated.consistency_flags = std::to_underlying(save::ConditionFlag::HasDeadline);
    set.conditions = { dated };
    simulation::ParkMessages messages;
    const auto keys = [&] {
        std::vector<std::string> out;
        for (const simulation::ParkMessage& m : std::exchange(messages.list, { }))
        {
            CHECK(m.field_key == "ObjectiveCondition_ParkGuests");
            out.push_back(m.text_key);
        }
        return out;
    };

    simulation::ParkFigures f { .guests = 500 };
    CHECK_FALSE(simulation::check_goals(set, f, 0, messages)); // Met, but not judged before its date
    CHECK(keys().empty());
    CHECK_FALSE(simulation::check_goals(set, f, 5 * kMonth, messages));
    CHECK(keys() == std::vector<std::string> { "ObjectiveMessage_MonthWarning" });
    CHECK_FALSE(simulation::check_goals(set, f, 5 * kMonth + 1, messages));
    CHECK(keys().empty());
    CHECK_FALSE(simulation::check_goals(set, f, 6 * kMonth - kWeek, messages));
    CHECK(keys() == std::vector<std::string> { "ObjectiveMessage_WeekWarning" });
    f.guests = 10;
    CHECK_FALSE(simulation::check_goals(set, f, 6 * kMonth, messages));
    CHECK(keys() == std::vector<std::string> { "ObjectiveMessage_TryAgainNextYear" });
    CHECK(set.conditions[0].next_check_time == doctest::Approx(6 * kMonth + kYear));
    CHECK_FALSE(set.conditions[0].completed);
    f.guests = 500;
    CHECK(simulation::check_goals(set, f, 6 * kMonth + kYear, messages));
    CHECK(keys() == std::vector<std::string> { "ObjectiveMessage_MetCondition" }); // No warnings once the date has come
}
