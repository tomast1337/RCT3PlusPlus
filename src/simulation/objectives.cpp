// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/objectives.hpp"

#include "simulation/park_state.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>

namespace rct3::simulation
{
    namespace
    {
        constexpr std::int32_t kLevels = 3;

        // Prizes by number: none, the nine a scenario may give for a level, then the levels' own.
        constexpr std::array<std::string_view, 13> kPrizeKeys = {
            "", "Prize_LargeCash", "Prize_SmallCash", "Prize_Lollipop", "Prize_ParkRatingBonus", "Prize_InterestRateDrop", "Prize_LoanIncrease", "Prize_ConstructionRights", "Prize_CSCoaster", "Prize_ResearchCatagory", "Prize_Apprentice", "Prize_Entrepreneur", "Prize_Tycoon",
        };

        std::optional<save::Objective&> objective_of(save::ObjectiveSet& set, std::int32_t level)
        {
            const auto found = std::ranges::find_if(set.objectives, [&](const save::Objective& o) { return o.difficulty == level && o.objective_id == 0; });
            return found != set.objectives.end() ? std::optional<save::Objective&>(*found) : std::nullopt;
        }

        /// The figure of a tested ride a coaster goal names, in the units the goal's value is kept in.
        std::optional<float> ride_figure(const save::Ride& ride, std::uint32_t figure)
        {
            constexpr float kGravity = 9.81f;
            if (!ride.stats.last_lap || !ride.stats.last_lap->valid)
                return std::nullopt;
            const save::TestResults& t = *ride.stats.last_lap;
            const save::Assessment& a = ride.assessment;
            if (figure <= 2 && !a.valid)
                return std::nullopt;
            switch (figure)
            {
                case 0:
                    return a.excitement;
                case 1:
                    return a.intensity;
                case 2:
                    return a.nausea;
                case 3:
                    return t.ride_length;
                case 4:
                    return t.max_negative_vertical / kGravity;
                case 5:
                    return t.max_positive_vertical / kGravity;
                case 6:
                    return t.max_lateral / kGravity;
                case 7:
                    return t.max_speed;
                case 8:
                {
                    const auto [low, high] = std::ranges::minmax_element(ride.graphs.altitude);
                    return low != ride.graphs.altitude.end() ? *high - *low : 0.0f;
                }
                case 9:
                    return t.air_time;
                case 10:
                    return static_cast<float>(t.inversion_count);
                case 11:
                    return t.highest_drop;
                default:
                    return std::nullopt;
            }
        }

        bool coaster_goal_met(const save::CoasterTarget& target, std::span<const GoalRide> rides)
        {
            const RideGroup group = (target.flags & save::CoasterTarget::kPoolSlides) != 0 ? RideGroup::PoolSlide : RideGroup::Coaster;
            std::int32_t passing = 0;
            for (const GoalRide& r : rides)
            {
                if (r.group != group || r.ride == nullptr)
                    continue;
                const std::optional<float> v = ride_figure(*r.ride, target.flags & save::CoasterTarget::kFigure);
                if (!v)
                    continue;
                const bool passes = (target.flags & save::CoasterTarget::kAtMost) != 0 ? *v <= target.value : *v >= target.value;
                if (passes)
                    ++passing;
                else if ((target.flags & save::CoasterTarget::kEveryRide) != 0)
                    return false;
            }
            return passing >= target.rides;
        }

        bool met_now(const save::Condition& c, const ParkFigures& f)
        {
            const auto compare = [&](std::int64_t now) { return c.value && (c.greater_than.value_or(true) ? now >= *c.value : now <= *c.value); };
            switch (c.kind)
            {
                case save::ConditionKind::ParkValue:
                    return compare(f.park_value);
                case save::ConditionKind::ParkGuests:
                    return compare(f.guests);
                case save::ConditionKind::ParkRating:
                    return compare(f.rating);
                case save::ConditionKind::RideIncome:
                    return compare(f.ride_income);
                case save::ConditionKind::ShopProfit:
                    return compare(f.shop_profit);
                case save::ConditionKind::RepayLoan:
                    return f.loan == 0;
                case save::ConditionKind::Coaster:
                    return c.coaster && coaster_goal_met(*c.coaster, f.rides);
                default:
                    // shortcut: goals about things the simulation does not have yet (VIP visits, items bought, animals,
                    // pools) are never met, so a level that sets one cannot be won; judge them as those parts arrive.
                    return false;
            }
        }
    } // namespace

    RideGroup ride_group(std::uint32_t kind) noexcept
    {
        switch (kind)
        {
            case 3:
            case 15:
            case 16:
            case 17:
                return RideGroup::Coaster;
            case 19:
                return RideGroup::PoolSlide;
            default:
                return RideGroup::Other;
        }
    }

    void count_takings(Takings& takings, float time, std::int64_t rides_so_far, std::int64_t shops_so_far)
    {
        const auto month = static_cast<std::int64_t>(std::floor(time / save::GameClock::kUnitsPerMonth));
        if (month == takings.month)
            return;
        // shortcut: the month counting begins in is only partly counted, so the first month's figures come out low;
        // keep the takings in the park file if goals on them should count from the start.
        if (takings.month >= 0)
        {
            takings.rides = rides_so_far - takings.rides_before;
            takings.shops = shops_so_far - takings.shops_before;
        }
        takings.month = month;
        takings.rides_before = rides_so_far;
        takings.shops_before = shops_so_far;
    }

    std::int32_t level_playing(const save::ObjectiveSet& set)
    {
        for (std::int32_t level = 0; level < kLevels - 1; ++level)
        {
            const bool won = std::ranges::any_of(set.objectives, [&](const save::Objective& o) { return o.difficulty == level && o.objective_id == 0 && o.success; });
            if (!won)
                return level;
        }
        return kLevels - 1;
    }

    std::optional<std::array<std::int32_t, 2>> check_goals(save::ObjectiveSet& set, const ParkFigures& figures, float time)
    {
        const std::int32_t level = level_playing(set);
        std::optional<save::Objective&> objective = objective_of(set, level);
        if (!objective || objective->success)
            return std::nullopt;
        bool any = false;
        bool all = true;
        for (save::Condition& c : set.conditions)
        {
            if (c.difficulty != level || c.objective_id != 0)
                continue;
            any = true;
            if (c.completed)
                continue;
            if (!met_now(c, figures))
            {
                // A break starts the count again.
                c.consistency_flags &= ~std::to_underlying(save::ConditionFlag::TimerRunning);
                all = false;
                continue;
            }
            if (!c.has(save::ConditionFlag::TimerRunning))
            {
                c.consistency_flags |= std::to_underlying(save::ConditionFlag::TimerRunning);
                c.start_time = time;
            }
            // shortcut: goals with a date to be judged on are judged as soon as they have held, dates ignored; no
            // scenario file sets one.
            if (time - c.start_time >= static_cast<float>(std::max(c.months_to_hold, 0)) * save::GameClock::kUnitsPerMonth)
            {
                c.completed = true;
                c.consistency_flags |= std::to_underlying(save::ConditionFlag::Satisfied);
            }
            else
                all = false;
        }
        if (!any || !all)
            return std::nullopt;
        objective->success = true;
        const std::int32_t prize = objective->prize_id > 0 && objective->prize_id < kApprenticePrize ? objective->prize_id : 0;
        return std::array { prize, kApprenticePrize + level };
    }

    void give_prize(ParkState& park, save::Finance& finance, std::int32_t id)
    {
        const tuning::Prizes& p = kTuning.prizes;
        switch (id)
        {
            case 1:
            case 2:
                if (park.cash)
                    *park.cash += id == 1 ? p.large_cash : p.small_cash;
                break;
            case 4:
                if (park.rating)
                    *park.rating = std::min(*park.rating + p.rating_bonus, 1.0f);
                break;
            case 5:
                finance.interest_rate_percent /= 2;
                break;
            case 6:
                finance.max_loan *= 2;
                break;
            default:
                // shortcut: land to buy, a coaster design and research boosts only tell the player for now; give them
                // once the land, design and research windows can.
                break;
        }
    }

    std::string_view prize_name(std::int32_t id)
    {
        return id > 0 && std::cmp_less(id, kPrizeKeys.size()) ? kPrizeKeys[static_cast<std::size_t>(id)] : std::string_view("Prize_Invalid");
    }

    std::string_view prize_message(std::int32_t id)
    {
        constexpr std::array<std::string_view, 13> kAwardKeys = {
            "", "Prize_LargeCashAward", "Prize_SmallCashAward", "Prize_LollipopAward", "Prize_ParkRatingBonusAward", "Prize_InterestRateDropAward", "Prize_LoanIncreaseAward", "Prize_ConstructionRightsAward", "Prize_CSCoasterAward", "Prize_ResearchCatagoryAward", "Prize_ApprenticeAward", "Prize_EntrepreneurAward", "Prize_TycoonAward",
        };
        return id > 0 && std::cmp_less(id, kAwardKeys.size()) ? kAwardKeys[static_cast<std::size_t>(id)] : std::string_view("Prize_InvalidAward");
    }

} // namespace rct3::simulation
