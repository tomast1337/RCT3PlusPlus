// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park_graphs.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation;

TEST_CASE("a week's end puts its figures in the week's slot and starts the next")
{
    save::ParkModel park;
    park.has_park = true;
    park.has_finance = true;
    park.park.guests_in_park = { .samples = { 90, 80, 70 }, .capacity = 3 };
    park.park.guest_intake = { .samples = { 12, 40 }, .capacity = 3 };
    park.finance.park_value = { .samples = { 5000 }, .capacity = 3 };
    close_week(park, { .guests = 100, .intake = 30, .income = 700, .profit = 500 });
    // A level starts the new week where it is; the oldest falls off past the capacity.
    CHECK(park.park.guests_in_park.samples == std::vector<float> { 100, 100, 80 });
    // A total over the week starts at nought.
    CHECK(park.park.guest_intake.samples == std::vector<float> { 0, 30, 40 });
    // What the simulation does not work out carries on.
    CHECK(park.finance.park_value.samples == std::vector<float> { 5000, 5000 });
    // Spent: what came in less what was made.
    CHECK(park.finance.expenditure.samples == std::vector<float> { 0, 200 });
    // A series the park file did not keep keeps our number of weeks.
    CHECK(park.finance.expenditure.capacity == kGraphWeeks);
}

TEST_CASE("a park without its records starts its graphs as a week ends")
{
    save::ParkModel park;
    close_week(park, { .guests = 100, .income = 50 });
    CHECK(park.park.guests_in_park.samples == std::vector<float> { 100, 100 });
    CHECK(park.park.guests_in_park.capacity == kGraphWeeks);
    CHECK(park.finance.income.samples == std::vector<float> { 0, 50 });
    // Nothing to carry on from.
    CHECK(park.finance.park_value.empty());
}

TEST_CASE("the ledger books into this month's period and starts a new one as each month begins")
{
    save::Finance money;
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
    CHECK(money.ledger_periods_loaded == save::kLedgerPeriods);
    ledger_month(money, kept, { .year = 9, .month = 0 });
    CHECK(std::ranges::all_of(money.ledger, [](const save::LedgerPeriod& p) { return p.staff_wages == 0; }));
}

TEST_CASE("a park whose file kept no ledger starts one at its first booking")
{
    save::Finance money;
    std::optional<std::int64_t> kept;
    ledger_month(money, kept, { }).staff_wages += 875;
    CHECK(money.ledger_periods_loaded == 1);
    CHECK(money.ledger[0].staff_wages == 875);
}
