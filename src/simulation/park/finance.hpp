// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's money: each change to its cash is booked on a line of the month's ledger in the same step, so the
// accounts always add up to what the cash did. The ledger keeps what came in as negative and what went out as
// positive, as the park file does.
#pragma once

#include "save/model/park.hpp"
#include "simulation/park/state.hpp"

#include <cstdint>
#include <optional>

namespace rct3::simulation::park
{

    /// A line of a ledger period (save::model::LedgerPeriod), one per counter.
    enum class LedgerLine
    {
        FoodDrinkSales,
        FoodDrinkStock,
        LandPurchase,
        Landscaping,
        LoanInterest,
        Marketing,
        Other,
        ParkEntranceTickets,
        PrizeMoney,
        Research,
        RideConstruction,
        RideRunningCosts,
        RideTickets,
        ShopSales,
        ShopStock,
        StaffTraining,
        StaffWages,
        PoolTickets,
        Billboards,
        ViewingGalleryTickets,
        OtherAnimalSpending,
    };

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

} // namespace rct3::simulation::park
