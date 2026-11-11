// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park/graphs.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation;
using namespace rct3::simulation::park;

TEST_CASE("a week's end puts its figures in the week's slot and starts the next")
{
    save::model::ParkModel park;
    park.has_park = true;
    park.has_finance = true;
    park.park.guests_in_park = { .samples = { 90, 80, 70 }, .capacity = 3 };
    park.park.guest_intake = { .samples = { 12, 40 }, .capacity = 3 };
    park.finance.share_price = { .samples = { 5000 }, .capacity = 3 };
    close_week(park, { .guests = 100, .intake = 30, .income = 700, .profit = 500 });
    // A level starts the new week where it is; the oldest falls off past the capacity.
    CHECK(park.park.guests_in_park.samples == std::vector<float> { 100, 100, 80 });
    // A total over the week starts at nought.
    CHECK(park.park.guest_intake.samples == std::vector<float> { 0, 30, 40 });
    // What the simulation does not work out carries on.
    CHECK(park.finance.share_price.samples == std::vector<float> { 5000, 5000 });
    // Spent: what came in less what was made.
    CHECK(park.finance.expenditure.samples == std::vector<float> { 0, 200 });
    // A series the park file did not keep keeps our number of weeks.
    CHECK(park.finance.expenditure.capacity == kGraphWeeks);
}

TEST_CASE("a park without its records starts its graphs as a week ends")
{
    save::model::ParkModel park;
    close_week(park, { .guests = 100, .income = 50 });
    CHECK(park.park.guests_in_park.samples == std::vector<float> { 100, 100 });
    CHECK(park.park.guests_in_park.capacity == kGraphWeeks);
    CHECK(park.finance.income.samples == std::vector<float> { 0, 50 });
    // Nothing to carry on from.
    CHECK(park.finance.share_price.empty());
}

TEST_CASE("the company is worth its cash less its loan and the park's value")
{
    save::model::ParkModel park;
    close_week(park, { .cash_less_loan = -2000, .park_value = 9000 });
    CHECK(park.finance.park_value.samples == std::vector<float> { 9000, 9000 });
    CHECK(park.finance.company_value.samples == std::vector<float> { 7000, 7000 });
}
