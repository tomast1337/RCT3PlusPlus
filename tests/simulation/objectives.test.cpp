// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/objectives.hpp"

#include "simulation/park/state.hpp"
#include "simulation/research.hpp"
#include "simulation/tuning.hpp"

#include <doctest/doctest.h>
#include <utility>

using namespace rct3;

namespace
{
    constexpr float kMonth = save::model::GameClock::kUnitsPerMonth;

    save::model::Condition goal(std::int32_t level, save::model::ConditionKind kind, std::int64_t value, std::int32_t months = 0)
    {
        return { .kind = kind, .difficulty = level, .months_to_hold = months, .value = value };
    }
} // namespace

TEST_CASE("a level is won once all its goals are met, and only then is the next one looked at")
{
    save::model::ObjectiveSet set;
    set.objectives = { { .difficulty = 0, .prize_id = 2 }, { .difficulty = 1, .prize_id = 6 } };
    set.conditions = { goal(0, save::model::ConditionKind::ParkGuests, 300), goal(0, save::model::ConditionKind::ParkRating, 600, 2), goal(1, save::model::ConditionKind::ParkGuests, 100) };

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
    save::model::ObjectiveSet set;
    set.objectives = { { .difficulty = 0 } };
    set.conditions = { goal(0, save::model::ConditionKind::ParkGuests, 0), goal(0, save::model::ConditionKind::PoolGuests, 0) };
    CHECK(simulation::judged(save::model::ConditionKind::ParkGuests));
    CHECK_FALSE(simulation::judged(save::model::ConditionKind::PoolGuests));
    CHECK(simulation::judged(save::model::ConditionKind::Vip));
    CHECK_FALSE(simulation::check_goals(set, { .guests = 10 }, 0));
    CHECK(set.conditions[0].completed);
    CHECK_FALSE(set.conditions[1].completed);
}

TEST_CASE("a coaster goal counts the tested coasters reaching its figure")
{
    save::model::Ride fast, slow, untested, wheel;
    fast.stats.last_lap = save::model::TestResults { .max_speed = 30, .valid = true };
    slow.stats.last_lap = save::model::TestResults { .max_speed = 15, .valid = true };
    wheel.stats.last_lap = save::model::TestResults { .max_speed = 40, .valid = true };
    const std::array rides { simulation::GoalRide { simulation::RideGroup::Coaster, &fast }, simulation::GoalRide { simulation::RideGroup::Coaster, &slow }, simulation::GoalRide { simulation::RideGroup::Coaster, &untested }, simulation::GoalRide { simulation::RideGroup::Other, &wheel } };
    const simulation::ParkFigures f { .rides = rides };
    const auto met = [&](save::model::CoasterTarget target) {
        save::model::ObjectiveSet set;
        set.objectives = { { .difficulty = 0 } };
        save::model::Condition c { .kind = save::model::ConditionKind::Coaster };
        c.coaster = target;
        set.conditions = { c };
        return simulation::check_goals(set, f, 0).has_value();
    };
    CHECK(met({ .value = 20, .flags = 7, .rides = 1 }));
    CHECK_FALSE(met({ .value = 20, .flags = 7, .rides = 2 })); // The wheel is no coaster
    CHECK(met({ .value = 20, .flags = 7 | save::model::CoasterTarget::kAtMost, .rides = 1 }));
    CHECK_FALSE(met({ .value = 20, .flags = 7 | save::model::CoasterTarget::kEveryRide, .rides = 1 }));
}

TEST_CASE("the goals on money read last month's ledger: ride tickets, and shop and food sales less their stock")
{
    const simulation::ParkFigures f { .last_month = { .food_drink_sales = -900, .food_drink_stock = 300, .park_entrance_tickets = -5000, .ride_tickets = -1200, .shop_sales = -100, .shop_stock = 60 } };
    CHECK(simulation::read_goal(goal(0, save::model::ConditionKind::RideIncome, 1000), f).now == 1200);
    CHECK(simulation::read_goal(goal(0, save::model::ConditionKind::ShopProfit, 1000), f).now == 640);
    CHECK_FALSE(simulation::read_goal(goal(0, save::model::ConditionKind::ShopProfit, 1000), f).met);
}

TEST_CASE("prizes pay out, their cash booked as prize money")
{
    simulation::park::ParkState park;
    park.cash = 0;
    park.rating = 0.99f;
    save::model::Finance finance { .max_loan = 1000, .interest_rate_percent = 8 };
    save::model::TerrainGrid land { .width = 2, .height = 1, .cells = { { .flags = std::to_underlying(save::model::TileFlag::HeldBack) }, { } } };
    std::optional<std::int64_t> kept;
    simulation::Research research;
    simulation::give_prize(park, finance, kept, land, research, 1);
    simulation::give_prize(park, finance, kept, land, research, 2);
    CHECK(park.cash > 0);
    CHECK(finance.ledger[0].prize_money == -*park.cash); // Income, below nought
    simulation::give_prize(park, finance, kept, land, research, 4);
    CHECK(park.rating == 1.0f);
    simulation::give_prize(park, finance, kept, land, research, 5);
    simulation::give_prize(park, finance, kept, land, research, 6);
    CHECK(finance.interest_rate_percent == 4);
    CHECK(finance.max_loan == 2000);
    simulation::give_prize(park, finance, kept, land, research, 7);
    CHECK(land.cells[0].has(save::model::TileFlag::ForSale));
    CHECK_FALSE(land.cells[1].has(save::model::TileFlag::ForSale));
    CHECK(simulation::prize_message(12) == "Prize_TycoonAward");
    CHECK(simulation::prize_name(0) == "Prize_Invalid");
}

TEST_CASE("the research prize is free progress at the next part of the budget paid")
{
    simulation::park::ParkState park;
    park.cash = 100000;
    save::model::Finance finance;
    save::model::TerrainGrid land;
    std::optional<std::int64_t> kept;
    simulation::Research research { .budget = 30000, .current = 1 };
    std::vector<simulation::ResearchItem> items { { .id = 1, .cost = 100000 } };
    random::RandomState dice = random::seeded(random::Seed { 1 });
    simulation::give_prize(park, finance, kept, land, research, 9);
    CHECK(research.boost == simulation::kTuning.prizes.research_boost.value);
    simulation::step_research(research, save::model::GameClock::kUnitsPerMonth / 30, items, park, finance, kept, dice);
    CHECK(research.boost == 0);
    CHECK(research.spent == 1000 + simulation::kTuning.prizes.research_boost.value);
    CHECK(park.cash == 100000 - 1000); // The boost itself costs nothing
}

TEST_CASE("a dated goal warns a month and a week before, is judged on its date, and a miss moves it a year on")
{
    constexpr float kWeek = save::model::GameClock::kUnitsPerWeek;
    constexpr float kYear = save::model::GameClock::kUnitsPerYear;
    save::model::ObjectiveSet set;
    set.objectives = { { .difficulty = 0 } };
    save::model::Condition dated = goal(0, save::model::ConditionKind::ParkGuests, 300);
    dated.next_check_time = 6 * kMonth;
    dated.consistency_flags = std::to_underlying(save::model::ConditionFlag::HasDeadline);
    set.conditions = { dated };
    simulation::park::ParkMessages messages;
    const auto keys = [&] {
        std::vector<std::string> out;
        for (const simulation::park::ParkMessage& m : std::exchange(messages.list, { }))
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

TEST_CASE("a goal on last month's takings holds for one month less, as that month already counts")
{
    save::model::ObjectiveSet set;
    set.objectives = { { .difficulty = 0 } };
    set.conditions = { goal(0, save::model::ConditionKind::RideIncome, 1000, 2) };
    const simulation::ParkFigures f { .last_month = { .ride_tickets = -1500 } };
    CHECK_FALSE(simulation::check_goals(set, f, 0));
    CHECK(simulation::months_held(set.conditions[0], 0.5f * kMonth) == doctest::Approx(0.5f));
    CHECK_FALSE(simulation::check_goals(set, f, 0.9f * kMonth));
    CHECK(simulation::check_goals(set, f, kMonth));
}

TEST_CASE("a goal reads where the park stands on it and whether that meets it")
{
    const simulation::ParkFigures f { .guests = 250, .loan = 500, .animal_health = 49, .dung = 12 };
    const simulation::GoalReading guests = simulation::read_goal(goal(0, save::model::ConditionKind::ParkGuests, 200), f);
    CHECK(guests.now == 250);
    CHECK(guests.met);
    CHECK_FALSE(simulation::read_goal(goal(0, save::model::ConditionKind::AnimalHealth, 50), f).met);
    save::model::Condition dung = goal(0, save::model::ConditionKind::Dung, 30);
    dung.greater_than = false; // At most
    CHECK(simulation::read_goal(dung, f).met);
    const simulation::GoalReading loan = simulation::read_goal({ .kind = save::model::ConditionKind::RepayLoan }, f);
    CHECK(loan.now == 500);
    CHECK_FALSE(loan.met);
    const simulation::GoalReading pool = simulation::read_goal(goal(0, save::model::ConditionKind::PoolGuests, 0), f);
    CHECK_FALSE(pool.now);
    CHECK_FALSE(pool.met);
    CHECK(simulation::judged(save::model::ConditionKind::AnimalHealth));
    CHECK_FALSE(simulation::judged(save::model::ConditionKind::AnimalsOfSpecies));
}

TEST_CASE("the rating prize lifts the rating at once and each week's score while it fades")
{
    simulation::park::ParkState park;
    park.rating = 0.5f;
    save::model::Finance finance;
    save::model::TerrainGrid land;
    std::optional<std::int64_t> kept;
    simulation::Research research;
    simulation::give_prize(park, finance, kept, land, research, 4);
    CHECK(park.rating > 0.5f);
    const float bonus = park.rating_bonus;
    REQUIRE(bonus > 0);
    simulation::park::rate_week(park, { }, { });
    CHECK(park.rating_bonus < bonus);
    for (int week = 0; week < 100 && park.rating_bonus > 0; ++week)
        simulation::park::rate_week(park, { }, { });
    CHECK(park.rating_bonus == 0);
}
