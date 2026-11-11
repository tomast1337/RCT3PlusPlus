// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's money: each change to its cash is booked on a line of the month's ledger in the same step, so the
// accounts always add up to what the cash did. The ledger keeps what came in as negative and what went out as
// positive, as the park file does.
#pragma once

#include "save/model/park.hpp"
#include "save/model/rides.hpp"
#include "simulation/park/ledger_line.hpp"
#include "simulation/park/state.hpp"

#include <cstdint>
#include <optional>

namespace rct3::simulation
{
    struct SimWorld;
} // namespace rct3::simulation

namespace rct3::simulation::park
{

    /// The ledger's period for the month `date` is in, the newest. As the calendar passes into later months, each
    /// starts a new period in front of the older ones, dropping the oldest past kLedgerPeriods. `kept` is the month,
    /// counted from the calendar's start, that the newest period stands for: none at first, when the newest is taken
    /// as this month's (as a park file leaves it).
    [[nodiscard]] save::model::LedgerPeriod& ledger_month(save::model::Finance& finance, std::optional<std::int64_t>& kept, const save::model::CalendarDate& date);

    /// Takes `cost` (hundredths; negative for money coming in) from `park`'s cash and books it on `line` of the
    /// month's ledger in `finance` (see ledger_month). A park whose money never runs out books nothing. Unless
    /// `forced`, a spend the park cannot afford is refused: nothing changes and false comes back. A forced one
    /// may leave the cash below nought.
    bool book(ParkState& park, save::model::Finance& finance, std::optional<std::int64_t>& kept, std::int64_t cost, LedgerLine line, bool forced);

    /// How much the loan moves at a time: 1,000.00 in hundredths. Chosen by eye: a few presses buy a ride.
    inline constexpr std::int64_t kLoanStep = 100'000;

    /// How much `park` can borrow (`borrow`) or repay in one step: kLoanStep, cut short to keep the loan between
    /// nought and `max_loan` (hundredths). A repayment needs the cash in hand. Nought when nothing can move, as in
    /// a park whose money never runs out.
    [[nodiscard]] std::int64_t loan_step(const ParkState& park, std::int64_t max_loan, bool borrow);

    /// Borrows or repays one loan_step: the cash and the loan move together, with no ledger line. Returns
    /// whether the loan changed.
    bool change_loan(ParkState& park, std::int64_t max_loan, bool borrow);

    /// What the park is worth, in hundredths: its guests, and each open ride by how exciting it was rated, a
    /// thrilling ride worth many gentle ones. Facilities, rides not yet rated, scenery, paths and land count for
    /// nothing. The company is worth this and its cash less its loan; where money never runs out, the cash counts
    /// for nothing.
    [[nodiscard]] std::int64_t park_value(std::int32_t guests, const save::model::RideModel& rides);

    /// A week's interest on `loan` (hundredths) at a yearly rate of `rate_percent`: the year's interest spread
    /// over kTuning.charges.interest_weeks weeks, rounded to the hundredth.
    [[nodiscard]] std::int64_t weekly_interest(std::int64_t loan, float rate_percent);

    /// What a ride built here costs to run each week, from what it cost to build (hundredths): a set share of it,
    /// larger for a `tracked` ride's track than for a flat ride.
    [[nodiscard]] std::int64_t running_cost(std::int64_t build_cost, bool tracked);

    /// As each week ends, charges the loan's interest and each open ride's running cost to Charges in the
    /// registry's context, for whoever shows the park to take from its cash, broken down or not. Stalls, facilities
    /// and pools pay none.
    /// shortcut: the week runs from when the park was opened, as the payroll's does.
    void step_charges(SimWorld& world);

} // namespace rct3::simulation::park
