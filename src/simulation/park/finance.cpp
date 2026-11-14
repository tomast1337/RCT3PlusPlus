// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park/finance.hpp"

#include "simulation/components.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/park/messages.hpp"
#include "simulation/park/state.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace rct3::simulation::park
{

    save::model::LedgerPeriod& ledger_month(save::model::Finance& finance, std::optional<std::int64_t>& kept, const save::model::CalendarDate& date)
    {
        constexpr auto kMonthsPerYear = static_cast<std::int64_t>(save::model::GameClock::kUnitsPerYear / save::model::GameClock::kUnitsPerMonth);
        const std::int64_t month = date.year * kMonthsPerYear + date.month;
        auto& ledger = finance.ledger;
        if (ledger.empty())
            ledger.emplace_back();
        for (std::int64_t n = std::min<std::int64_t>(month - kept.value_or(month), save::model::kKeptMonths); n > 0; --n)
        {
            // The counters only some parks have stay shown in the months that follow.
            const save::model::LedgerPeriod& before = ledger.front();
            const auto zero = [](std::int32_t) { return 0; };
            const save::model::LedgerPeriod next { .pool_tickets = before.pool_tickets.transform(zero), .billboards = before.billboards.transform(zero), .viewing_gallery_tickets = before.viewing_gallery_tickets.transform(zero), .other_animal_spending = before.other_animal_spending.transform(zero) };
            ledger.insert(ledger.begin(), next);
            if (ledger.size() > save::model::kKeptMonths)
                ledger.pop_back();
        }
        kept = std::max(kept.value_or(month), month);
        return ledger.front();
    }

    void cannot_pay(ParkState& park)
    {
        if (std::ranges::none_of(park.messages.list, [](const ParkMessage& m) { return m.text_key == kCannotPayKey && m.age.value < kCannotPayGap.value; }))
            post(park.messages, { .text_key = std::string(kCannotPayKey) });
    }

    bool book(ParkState& park, save::model::Finance& finance, std::optional<std::int64_t>& kept, units::Money cost, LedgerLine line, Charge charge)
    {
        if (!park.cash)
            return true;
        if (charge == Charge::IfAffordable && cost > units::Money { } && *park.cash < cost)
        {
            cannot_pay(park);
            return false;
        }
        *park.cash -= cost;
        save::model::LedgerPeriod& p = ledger_month(finance, kept, park.date);
        const auto c = static_cast<std::int32_t>(cost.value); // The ledger keeps 32-bit counters, as the park file does
        switch (line)
        {
            case LedgerLine::FoodDrinkSales:
                p.food_drink_sales += c;
                break;
            case LedgerLine::FoodDrinkStock:
                p.food_drink_stock += c;
                break;
            case LedgerLine::LandPurchase:
                p.land_purchase += c;
                break;
            case LedgerLine::Landscaping:
                p.landscaping += c;
                break;
            case LedgerLine::LoanInterest:
                p.loan_interest += c;
                break;
            case LedgerLine::Marketing:
                p.marketing += c;
                break;
            case LedgerLine::Other:
                p.other += c;
                break;
            case LedgerLine::ParkEntranceTickets:
                p.park_entrance_tickets += c;
                break;
            case LedgerLine::PrizeMoney:
                p.prize_money += c;
                break;
            case LedgerLine::Research:
                p.research += c;
                break;
            case LedgerLine::RideConstruction:
                p.ride_construction += c;
                break;
            case LedgerLine::RideRunningCosts:
                p.ride_running_costs += c;
                break;
            case LedgerLine::RideTickets:
                p.ride_tickets += c;
                break;
            case LedgerLine::ShopSales:
                p.shop_sales += c;
                break;
            case LedgerLine::ShopStock:
                p.shop_stock += c;
                break;
            case LedgerLine::StaffTraining:
                p.staff_training += c;
                break;
            case LedgerLine::StaffWages:
                p.staff_wages += c;
                break;
            case LedgerLine::PoolTickets:
                p.pool_tickets = p.pool_tickets.value_or(0) + c;
                break;
            case LedgerLine::Billboards:
                p.billboards = p.billboards.value_or(0) + c;
                break;
            case LedgerLine::ViewingGalleryTickets:
                p.viewing_gallery_tickets = p.viewing_gallery_tickets.value_or(0) + c;
                break;
            case LedgerLine::OtherAnimalSpending:
                p.other_animal_spending = p.other_animal_spending.value_or(0) + c;
                break;
        }
        return true;
    }

    units::Money loan_step(const ParkState& park, units::Money max_loan, LoanMove move)
    {
        if (!park.cash)
            return { };
        if (move == LoanMove::Borrow)
            return std::clamp(max_loan - park.loan, units::Money { }, kLoanStep);
        const units::Money step = std::clamp(park.loan, units::Money { }, kLoanStep);
        return *park.cash >= step ? step : units::Money { };
    }

    bool change_loan(ParkState& park, units::Money max_loan, LoanMove move)
    {
        const units::Money step = loan_step(park, max_loan, move);
        if (step == units::Money { })
            return false;
        *park.cash += move == LoanMove::Borrow ? step : -step;
        park.loan += move == LoanMove::Borrow ? step : -step;
        return true;
    }

    units::Money park_value(std::int32_t guests, const save::model::RideModel& rides)
    {
        float worth = 0;
        for (const save::model::Ride& ride : rides.rides)
            if (ride.kind != save::model::RideKind::Special && !ride.base.demolished && ride.base.state == save::model::AttractionState::Open && ride.assessment.valid)
                worth += ride.assessment.excitement * ride.assessment.excitement;
        return { std::max(guests, 0) * kTuning.park_value.per_guest.value + std::llround(worth * static_cast<float>(kTuning.park_value.per_excitement.value)) };
    }

    units::Money weekly_interest(units::Money loan, float rate_percent)
    {
        return { std::llround(static_cast<double>(loan.value) * static_cast<double>(rate_percent) / 100 / kTuning.charges.interest_weeks) };
    }

    units::Money running_cost(units::Money build_cost, bool tracked)
    {
        return { std::llround(static_cast<double>(build_cost.value) * (tracked ? kTuning.charges.tracked_running_share : kTuning.charges.flat_running_share)) };
    }

    void step_charges(SimWorld& world)
    {
        constexpr float kSecondsPerWeek = kSecondsPerHour * save::model::GameClock::kUnitsPerWeek / kCalendarUnitsPerHour;
        entt::registry& r = world.registry;
        auto& charges = r.ctx().emplace<Charges>();
        for (charges.week.value += kTickSeconds; charges.week.value >= kSecondsPerWeek; charges.week.value -= kSecondsPerWeek)
        {
            charges.interest += weekly_interest(charges.loan, charges.rate);
            // One broken down is shut to guests but still open to the player, and goes on costing.
            for (const auto [e, a] : r.view<const Attraction>().each())
                if (const auto* upkeep = r.try_get<Upkeep>(e); a.open || (upkeep != nullptr && upkeep->broken))
                    charges.running += a.running_cost;
        }
    }

} // namespace rct3::simulation::park
