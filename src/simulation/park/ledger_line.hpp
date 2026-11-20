// SPDX-License-Identifier: GPL-3.0-or-later
//
// The lines of the park's ledger, apart from the ledger itself so that what counts money by line needs no more.
#pragma once

#include "core/types.hpp"

#include <cstddef>
#include <utility>

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
        AnimalPurchaseSale, ///< The animal lines, kept for each species too
        AnimalFood,
        AnimalAdoptions,
        OtherAnimalSpending,
    };

    /// How many lines LedgerLine has.
    inline constexpr usize kLedgerLines = std::to_underlying(LedgerLine::OtherAnimalSpending) + 1;

} // namespace rct3::simulation::park
