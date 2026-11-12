// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park/finance.hpp"

#include "simulation/components.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/park/state.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation;
using namespace rct3::simulation::park;

TEST_CASE("a park whose money never runs out books nothing")
{
    ParkState park;
    save::model::Finance money;
    std::optional<std::int64_t> kept;
    CHECK(book(park, money, kept, 5000, LedgerLine::StaffTraining, false));
    CHECK_FALSE(park.cash.has_value());
    CHECK(money.ledger.empty());
}

TEST_CASE("a spend the park cannot afford is refused and changes nothing")
{
    ParkState park;
    park.cash = 4000;
    save::model::Finance money;
    std::optional<std::int64_t> kept;
    CHECK_FALSE(book(park, money, kept, 5000, LedgerLine::StaffTraining, false));
    CHECK(park.cash == 4000);
    CHECK(money.ledger.empty());
    CHECK(book(park, money, kept, 4000, LedgerLine::StaffTraining, false));
    CHECK(park.cash == 0);
    CHECK(money.ledger[0].staff_training == 4000);
}

TEST_CASE("a refused spend tells the player, once in a while however often it is tried")
{
    ParkState park;
    park.cash = 100;
    save::model::Finance money;
    std::optional<std::int64_t> kept;
    const auto told = [&] { return std::ranges::count(park.messages.list, kCannotPayKey, &ParkMessage::text_key); };
    for (int i = 0; i < 10; ++i)
        CHECK_FALSE(book(park, money, kept, 300, LedgerLine::Landscaping, false));
    CHECK(told() == 1);
    age(park.messages, kCannotPayGap / 2);
    CHECK_FALSE(book(park, money, kept, 300, LedgerLine::Landscaping, false));
    CHECK(told() == 1);
    age(park.messages, kCannotPayGap);
    CHECK_FALSE(book(park, money, kept, 300, LedgerLine::Landscaping, false));
    CHECK(told() == 2);
    // Forced or paid for, nothing is said.
    CHECK(book(park, money, kept, 300, LedgerLine::StaffWages, true));
    age(park.messages, kCannotPayGap);
    CHECK(book(park, money, kept, -50, LedgerLine::RideTickets, false));
    CHECK(told() == 2);
}

TEST_CASE("a forced spend may take the cash below nought")
{
    ParkState park;
    park.cash = 100;
    save::model::Finance money;
    std::optional<std::int64_t> kept;
    CHECK(book(park, money, kept, 300, LedgerLine::StaffWages, true));
    CHECK(park.cash == -200);
    CHECK(money.ledger[0].staff_wages == 300);
}

TEST_CASE("money coming in adds to the cash and books negative")
{
    ParkState park;
    park.cash = -200;
    save::model::Finance money;
    std::optional<std::int64_t> kept;
    CHECK(book(park, money, kept, -250, LedgerLine::RideTickets, false));
    CHECK(park.cash == 50);
    CHECK(money.ledger[0].ride_tickets == -250);
    // A line only some parks have starts when first booked.
    CHECK(book(park, money, kept, -30, LedgerLine::PoolTickets, false));
    CHECK(money.ledger[0].pool_tickets == -30);
}

TEST_CASE("the ledger books into this month's period and starts a new one as each month begins")
{
    save::model::Finance money;
    // This month so far, as the park file left it, and the month before.
    money.ledger = { { .staff_wages = 700, .pool_tickets = -50 }, { .staff_wages = 900 } };
    std::optional<std::int64_t> kept;
    ledger_month(money, kept, { .year = 1, .month = 3 }).staff_wages += 100;
    CHECK(money.ledger[0].staff_wages == 800);
    CHECK(money.ledger.size() == 2);
    // The next month: the older ones move back a place.
    ledger_month(money, kept, { .year = 1, .month = 4 }).staff_training += 5000;
    CHECK(money.ledger[0].staff_wages == 0);
    CHECK(money.ledger[0].staff_training == 5000);
    CHECK(money.ledger[0].pool_tickets == 0); // A counter the park has stays shown
    CHECK_FALSE(money.ledger[0].billboards.has_value());
    CHECK(money.ledger[1].staff_wages == 800);
    CHECK(money.ledger[2].staff_wages == 900);
    CHECK(money.ledger.size() == 3);
    // Across the year's end, each month passed a period of its own, more than a park file keeps.
    ledger_month(money, kept, { .year = 2, .month = 0 }).staff_wages += 1;
    CHECK(money.ledger[4].staff_training == 5000);
    CHECK(money.ledger.size() == 7);
    CHECK(money.ledger.size() > save::model::kLedgerPeriods);
    // Months passed in one go keep only as many as the ledger holds.
    (void)ledger_month(money, kept, { .year = 90, .month = 0 });
    CHECK(money.ledger.size() == save::model::kKeptMonths);
    CHECK(std::ranges::all_of(money.ledger, [](const save::model::LedgerPeriod& p) { return p.staff_wages == 0; }));
}

TEST_CASE("a park whose file kept no ledger starts one at its first booking")
{
    save::model::Finance money;
    std::optional<std::int64_t> kept;
    ledger_month(money, kept, { }).staff_wages += 875;
    CHECK(money.ledger.size() == 1);
    CHECK(money.ledger[0].staff_wages == 875);
}

TEST_CASE("the loan moves a step at a time between nought and the maximum, cash and loan together")
{
    ParkState park { .cash = 50'000, .loan = 0 };
    CHECK_FALSE(change_loan(park, 250'000, false)); // Nothing to repay
    CHECK(change_loan(park, 250'000, true));
    CHECK(park.cash == 150'000);
    CHECK(park.loan == kLoanStep);
    CHECK(change_loan(park, 250'000, true));
    CHECK(change_loan(park, 250'000, true)); // The last step is cut short at the maximum
    CHECK(park.loan == 250'000);
    CHECK(park.cash == 300'000);
    CHECK(loan_step(park, 250'000, true) == 0);
    CHECK_FALSE(change_loan(park, 250'000, true));
    CHECK(park.loan == 250'000);
    CHECK(change_loan(park, 250'000, false));
    CHECK(park.loan == 150'000);
    CHECK(park.cash == 200'000);
}

TEST_CASE("repaying the loan needs the cash in hand")
{
    ParkState park { .cash = kLoanStep - 1, .loan = 300'000 };
    CHECK(loan_step(park, 500'000, false) == 0);
    CHECK_FALSE(change_loan(park, 500'000, false));
    CHECK(park.cash == kLoanStep - 1);
    CHECK(park.loan == 300'000);
    park.loan = 40'000; // Less than a step left: only that is repaid
    CHECK(change_loan(park, 500'000, false));
    CHECK(park.loan == 0);
    CHECK(park.cash == kLoanStep - 1 - 40'000);
}

TEST_CASE("a park whose money never runs out has no loan to change")
{
    ParkState park { .loan = 0 };
    CHECK(loan_step(park, 500'000, true) == 0);
    CHECK_FALSE(change_loan(park, 500'000, true));
    CHECK(park.loan == 0);
}

TEST_CASE("a park is worth its guests and its open rides by their excitement")
{
    const auto ride = [](save::model::RideKind kind, save::model::AttractionState state, float excitement) {
        save::model::Ride r { .kind = kind };
        r.base.state = state;
        r.assessment = { .valid = true, .excitement = excitement };
        return r;
    };
    save::model::RideModel rides;
    const auto guest = kTuning.park_value.per_guest.value;
    const auto point = kTuning.park_value.per_excitement.value;
    CHECK(park_value(10, rides) == 10 * guest);
    rides.rides.push_back(ride(save::model::RideKind::Tracked, save::model::AttractionState::Open, 2));
    CHECK(park_value(10, rides) == 10 * guest + 4 * point);
    // Twice as exciting, four times the worth.
    rides.rides.push_back(ride(save::model::RideKind::Flat, save::model::AttractionState::Open, 4));
    CHECK(park_value(0, rides) == 20 * point);
    // Closed, untested, pulled down, and facilities: nothing.
    rides.rides.push_back(ride(save::model::RideKind::Tracked, save::model::AttractionState::Closed, 5));
    rides.rides.push_back(ride(save::model::RideKind::Special, save::model::AttractionState::Open, 5));
    rides.rides.push_back(ride(save::model::RideKind::Flat, save::model::AttractionState::Open, 5));
    rides.rides.back().assessment.valid = false;
    rides.rides.push_back(ride(save::model::RideKind::Flat, save::model::AttractionState::Open, 5));
    rides.rides.back().base.demolished = true;
    CHECK(park_value(0, rides) == 20 * point);
}

namespace
{
    constexpr float kWeek = kSecondsPerHour * save::model::GameClock::kUnitsPerWeek / kCalendarUnitsPerHour;
} // namespace

TEST_CASE("a loan is charged a week's interest as each week ends, and no loan none")
{
    CHECK(weekly_interest(1'000'000, 6) == std::llround(1'000'000 * 0.06 / kTuning.charges.interest_weeks));
    CHECK(weekly_interest(0, 6) == 0);
    Sandbox park;
    auto& charges = park.world().registry.ctx().emplace<Charges>(Charges { .loan = 1'000'000, .rate = 6 });
    park.run_seconds(kWeek * 0.9f);
    CHECK(charges.interest.value == 0);
    park.run_seconds(kWeek * 0.2f);
    CHECK(charges.interest.value == weekly_interest(1'000'000, 6));
    CHECK(charges.interest.value > 0);
    charges.loan = 0;
    park.run_seconds(kWeek);
    CHECK(charges.interest.value == weekly_interest(1'000'000, 6));
}

TEST_CASE("an open ride costs its running cost each week; a closed ride nothing")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    auto& r = park.world().registry;
    r.get<Attraction>(park.flat_ride("Twister", { 1, 0 }, 0)).running_cost = { 500 };
    const auto closed = park.flat_ride("Swinger", { 2, 0 }, 0);
    r.get<Attraction>(closed).running_cost = { 700 };
    r.get<Attraction>(closed).open = false;
    (void)park.shop("Burgers", Product::Food, { 3, 0 }, 100);
    park.run_seconds(kWeek * 2.1f);
    CHECK(r.ctx().get<Charges>().running.value == 2 * 500);
    // Broken down, it is shut to guests but still costs.
    const auto broken = park.flat_ride("Breaker", { 4, 0 }, 0);
    r.get<Attraction>(broken).running_cost = { 300 };
    r.get<Attraction>(broken).open = false;
    r.emplace<Upkeep>(broken, Upkeep { .broken = true, .inspect_every = -1 });
    park.run_seconds(kWeek);
    CHECK(r.ctx().get<Charges>().running.value == 3 * 500 + 300);
}

TEST_CASE("an open facility costs its running cost each week; a closed one and a stall nothing")
{
    Sandbox park;
    park.path({ 0, 0 }, { 4, 0 });
    auto& r = park.world().registry;
    // A facility is an attraction guests use like a shop, carrying a running cost; a stall carries none.
    const auto toilet = park.shop("Toilet", Product::Toilet, { 1, 0 }, 0);
    r.get<Attraction>(toilet).running_cost = kTuning.charges.facility_running_cost;
    const auto shut = park.shop("Toilet", Product::Toilet, { 2, 0 }, 0);
    r.get<Attraction>(shut).running_cost = kTuning.charges.facility_running_cost;
    r.get<Attraction>(shut).open = false;
    (void)park.shop("Burgers", Product::Food, { 3, 0 }, 100);
    park.run_seconds(kWeek * 1.1f);
    CHECK(r.ctx().get<Charges>().running.value == kTuning.charges.facility_running_cost.value);
}

TEST_CASE("a ride built here costs a share of its build cost to run")
{
    CHECK(running_cost(0, true) == 0);
    CHECK(running_cost(1'000'000, true) == std::llround(1'000'000 * kTuning.charges.tracked_running_share));
    CHECK(running_cost(1'000'000, false) == std::llround(1'000'000 * kTuning.charges.flat_running_share));
    CHECK(running_cost(1'000'000, false) > 0);
    CHECK(running_cost(2'000'000, true) == 2 * running_cost(1'000'000, true));
}

TEST_CASE("a park whose money never runs out pays no interest or running costs")
{
    ParkState park;
    save::model::Finance money;
    std::optional<std::int64_t> kept;
    CHECK(book(park, money, kept, weekly_interest(1'000'000, 6), LedgerLine::LoanInterest, true));
    CHECK(book(park, money, kept, 500, LedgerLine::RideRunningCosts, true));
    CHECK(money.ledger.empty());
}
