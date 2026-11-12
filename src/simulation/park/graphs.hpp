// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The park's weekly graphs, kept in its own series so the park file and the graph windows agree. Each series'
// newest sample is the week still running; as a week ends that sample takes the week's figure and a new one
// starts in front of it, dropping the oldest past the series' capacity. A level (guests, cash, rating) starts the
// new week at its value now; a total over the week (intake, income, profit) starts it at nought.

#include "save/model/park.hpp"

#include <cstdint>

namespace rct3::simulation::park
{

    /// Weeks a park file's graphs hold, the span the graph windows show at first.
    inline constexpr std::uint32_t kFileWeeks = 64;
    /// Weeks a graph keeps: more than a park file's, whose series grow to it as the park runs.
    inline constexpr auto kGraphWeeks = static_cast<std::uint32_t>(save::model::kKeptWeeks);

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
        std::int64_t park_value = 0;     ///< Now (see park_value)
    };

    /// Ends the week in `park`'s graphs with `figures`, starting any series it has none of. The company is worth
    /// its cash less its loan and the park's value. Share price and publicity, which the simulation does not work
    /// out, carry on as they were.
    void close_week(save::model::ParkModel& park, const WeekFigures& figures);

} // namespace rct3::simulation::park
