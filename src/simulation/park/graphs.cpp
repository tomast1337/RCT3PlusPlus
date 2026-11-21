// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park/graphs.hpp"

#include "core/types.hpp"

#include <algorithm>

namespace rct3::simulation::park
{

    namespace
    {
        /// Ends the week in `series` with `value`, starting the next at `next`.
        void close(save::model::Series& series, f32 value, f32 next)
        {
            series.capacity = std::max(series.capacity, kGraphWeeks);
            if (series.samples.empty())
                series.samples.push_back(value);
            else
                series.samples.front() = value;
            series.push(next);
        }

        void level(save::model::Series& series, f32 value)
        {
            close(series, value, value);
        }

        void total(save::model::Series& series, f32 value)
        {
            close(series, value, 0);
        }

        void carry(save::model::Series& series)
        {
            if (!series.empty())
                level(series, series.samples.front());
        }
    } // namespace

    void close_week(save::model::ParkModel& park, const WeekFigures& f)
    {
        // A park the file kept no records for, such as a new sandbox, starts its graphs empty.
        {
            save::model::Park& p = park.park;
            level(p.park_rating, static_cast<f32>(f.rating));
            level(p.guests_in_park, static_cast<f32>(f.guests));
            level(p.adults_in_park, static_cast<f32>(f.adults));
            level(p.teenagers_in_park, static_cast<f32>(f.teenagers));
            level(p.children_in_park, static_cast<f32>(f.children));
            total(p.guest_intake, static_cast<f32>(f.intake));
            if (f.publicity)
                level(p.publicity_level, *f.publicity);
            else
                carry(p.publicity_level);
        }
        {
            save::model::Finance& m = park.finance;
            level(m.cash_less_loan, static_cast<f32>(f.cash_less_loan.value));
            total(m.income, static_cast<f32>(f.income.value));
            total(m.weekly_profit, static_cast<f32>(f.profit.value));
            total(m.expenditure, static_cast<f32>(std::max<i64>(0, (f.income - f.profit).value)));
            level(m.park_value, static_cast<f32>(f.park_value.value));
            level(m.company_value, static_cast<f32>((f.cash_less_loan + f.park_value).value));
            carry(m.share_price);
        }
    }

} // namespace rct3::simulation::park
