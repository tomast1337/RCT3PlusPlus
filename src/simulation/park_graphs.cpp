// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park_graphs.hpp"

#include <algorithm>

namespace rct3::simulation
{

    namespace
    {
        /// Ends the week in `series` with `value`, starting the next at `next`.
        void close(save::model::Series& series, float value, float next)
        {
            if (series.capacity == 0)
                series.capacity = kGraphWeeks;
            if (series.samples.empty())
                series.samples.push_back(value);
            else
                series.samples.front() = value;
            series.push(next);
        }

        void level(save::model::Series& series, float value)
        {
            close(series, value, value);
        }

        void total(save::model::Series& series, float value)
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
            level(p.park_rating, static_cast<float>(f.rating));
            level(p.guests_in_park, static_cast<float>(f.guests));
            level(p.adults_in_park, static_cast<float>(f.adults));
            level(p.teenagers_in_park, static_cast<float>(f.teenagers));
            level(p.children_in_park, static_cast<float>(f.children));
            total(p.guest_intake, static_cast<float>(f.intake));
            carry(p.publicity_level);
        }
        {
            save::model::Finance& m = park.finance;
            level(m.cash_less_loan, static_cast<float>(f.cash_less_loan));
            total(m.income, static_cast<float>(f.income));
            total(m.weekly_profit, static_cast<float>(f.profit));
            total(m.expenditure, static_cast<float>(std::max<std::int64_t>(0, f.income - f.profit)));
            carry(m.park_value);
            carry(m.company_value);
            carry(m.share_price);
        }
    }

    save::model::LedgerPeriod& ledger_month(save::model::Finance& finance, std::optional<std::int64_t>& kept, const save::model::CalendarDate& date)
    {
        constexpr auto kMonthsPerYear = static_cast<std::int64_t>(save::model::GameClock::kUnitsPerYear / save::model::GameClock::kUnitsPerMonth);
        const std::int64_t month = date.year * kMonthsPerYear + date.month;
        auto& ledger = finance.ledger;
        for (std::int64_t n = std::min<std::int64_t>(month - kept.value_or(month), save::model::kLedgerPeriods); n > 0; --n)
        {
            std::shift_right(ledger.begin(), ledger.end(), 1);
            // The counters only some parks have stay shown in the months that follow.
            const save::model::LedgerPeriod& before = ledger[1];
            const auto zero = [](std::int32_t) { return 0; };
            ledger[0] = { .pool_tickets = before.pool_tickets.transform(zero), .billboards = before.billboards.transform(zero), .viewing_gallery_tickets = before.viewing_gallery_tickets.transform(zero), .other_animal_spending = before.other_animal_spending.transform(zero) };
            finance.ledger_periods_loaded = std::min(finance.ledger_periods_loaded + 1, save::model::kLedgerPeriods);
        }
        kept = std::max(kept.value_or(month), month);
        finance.ledger_periods_loaded = std::max<std::size_t>(finance.ledger_periods_loaded, 1);
        return ledger[0];
    }

} // namespace rct3::simulation
