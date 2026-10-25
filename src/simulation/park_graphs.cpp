// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park_graphs.hpp"

#include <algorithm>

namespace rct3::simulation
{

    namespace
    {
        /// Ends the week in `series` with `value`, starting the next at `next`.
        void close(save::Series& series, float value, float next)
        {
            if (series.capacity == 0)
                series.capacity = kGraphWeeks;
            if (series.samples.empty())
                series.samples.push_back(value);
            else
                series.samples.front() = value;
            series.push(next);
        }

        void level(save::Series& series, float value)
        {
            close(series, value, value);
        }

        void total(save::Series& series, float value)
        {
            close(series, value, 0);
        }

        void carry(save::Series& series)
        {
            if (!series.empty())
                level(series, series.samples.front());
        }
    } // namespace

    void close_week(save::ParkModel& park, const WeekFigures& f)
    {
        if (park.has_park)
        {
            save::Park& p = park.park;
            level(p.park_rating, static_cast<float>(f.rating));
            level(p.guests_in_park, static_cast<float>(f.guests));
            level(p.adults_in_park, static_cast<float>(f.adults));
            level(p.teenagers_in_park, static_cast<float>(f.teenagers));
            level(p.children_in_park, static_cast<float>(f.children));
            total(p.guest_intake, static_cast<float>(f.intake));
            carry(p.publicity_level);
        }
        if (park.has_finance)
        {
            save::Finance& m = park.finance;
            level(m.cash_less_loan, static_cast<float>(f.cash_less_loan));
            total(m.income, static_cast<float>(f.income));
            total(m.weekly_profit, static_cast<float>(f.profit));
            total(m.expenditure, static_cast<float>(std::max<std::int64_t>(0, f.income - f.profit)));
            carry(m.park_value);
            carry(m.company_value);
            carry(m.share_price);
        }
    }

} // namespace rct3::simulation
