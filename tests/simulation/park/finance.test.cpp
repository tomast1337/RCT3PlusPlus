// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park/finance.hpp"

#include <algorithm>
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
    CHECK(money.ledger[0].staff_training == 0);
    CHECK(money.ledger_periods_loaded == 0);
}

TEST_CASE("a spend the park cannot afford is refused and changes nothing")
{
    ParkState park;
    park.cash = 4000;
    save::model::Finance money;
    std::optional<std::int64_t> kept;
    CHECK_FALSE(book(park, money, kept, 5000, LedgerLine::StaffTraining, false));
    CHECK(park.cash == 4000);
    CHECK(money.ledger[0].staff_training == 0);
    CHECK(book(park, money, kept, 4000, LedgerLine::StaffTraining, false));
    CHECK(park.cash == 0);
    CHECK(money.ledger[0].staff_training == 4000);
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
    money.ledger[0].staff_wages = 700; // This month so far, as the park file left it
    money.ledger[0].pool_tickets = -50;
    money.ledger[1].staff_wages = 900;
    money.ledger_periods_loaded = 2;
    std::optional<std::int64_t> kept;
    ledger_month(money, kept, { .year = 1, .month = 3 }).staff_wages += 100;
    CHECK(money.ledger[0].staff_wages == 800);
    CHECK(money.ledger_periods_loaded == 2);
    // The next month: the older ones move back a place.
    ledger_month(money, kept, { .year = 1, .month = 4 }).staff_training += 5000;
    CHECK(money.ledger[0].staff_wages == 0);
    CHECK(money.ledger[0].staff_training == 5000);
    CHECK(money.ledger[0].pool_tickets == 0); // A counter the park has stays shown
    CHECK_FALSE(money.ledger[0].billboards.has_value());
    CHECK(money.ledger[1].staff_wages == 800);
    CHECK(money.ledger[2].staff_wages == 900);
    CHECK(money.ledger_periods_loaded == 3);
    // Across the year's end, and months passed in one go keep only as many as the ledger holds.
    ledger_month(money, kept, { .year = 2, .month = 0 }).staff_wages += 1;
    CHECK(money.ledger[4].staff_training == 5000);
    CHECK(money.ledger_periods_loaded == save::model::kLedgerPeriods);
    (void)ledger_month(money, kept, { .year = 9, .month = 0 });
    CHECK(std::ranges::all_of(money.ledger, [](const save::model::LedgerPeriod& p) { return p.staff_wages == 0; }));
}

TEST_CASE("a park whose file kept no ledger starts one at its first booking")
{
    save::model::Finance money;
    std::optional<std::int64_t> kept;
    ledger_month(money, kept, { }).staff_wages += 875;
    CHECK(money.ledger_periods_loaded == 1);
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
