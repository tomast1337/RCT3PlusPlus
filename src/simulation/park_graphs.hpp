// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The park's weekly graphs, kept in its own series so the park file and the graph windows agree. Each series'
// newest sample is the week still running; as a week ends that sample takes the week's figure and a new one
// starts in front of it, dropping the oldest past the series' capacity. A level (guests, cash, rating) starts the
// new week at its value now; a total over the week (intake, income, profit) starts it at nought.

#include "save/model/park.hpp"

#include <cstdint>
#include <optional>

namespace rct3::simulation
{

    /// Weeks a graph keeps when the park file gave its series none.
    inline constexpr std::uint32_t kGraphWeeks = 64;

    /// The park's figures as a week ends. Money in hundredths, like cash.
    struct WeekFigures
    {
        std::int32_t rating = 0; ///< 0..kMaxRating
        std::int32_t guests = 0;
        std::int32_t adults = 0;
        std::int32_t teenagers = 0;
        std::int32_t children = 0;
        std::int32_t intake = 0;         ///< Guests who came in during the week
        std::int64_t cash_less_loan = 0; ///< Now
        std::int64_t income = 0;         ///< Taken during the week
        std::int64_t profit = 0;         ///< Change in cash over the week
    };

    /// Ends the week in `park`'s graphs with `figures`, starting any series it has none of. Park value, company value, share price and publicity,
    /// which the simulation does not work out, carry on as they were.
    void close_week(save::model::ParkModel& park, const WeekFigures& figures);

    /// The ledger's period for the month `date` is in, the newest. As the calendar passes into later months, each
    /// starts a new period in front of the older ones, dropping the oldest past kLedgerPeriods. `kept` is the month,
    /// counted from the calendar's start, that the newest period stands for: none at first, when the newest is taken
    /// as this month's (as a park file leaves it).
    [[nodiscard]] save::model::LedgerPeriod& ledger_month(save::model::Finance& finance, std::optional<std::int64_t>& kept, const save::model::CalendarDate& date);

} // namespace rct3::simulation
