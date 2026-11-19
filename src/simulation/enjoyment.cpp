// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/enjoyment.hpp"

#include "core/types.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rct3::simulation
{

    Fit fit(f32 rating, f32 least, f32 most, f32 slack)
    {
        const f32 off = std::max({ (least - rating) / static_cast<f32>(kTuning.enjoyment.tame_reach.value), rating - most, 0.0f });
        return off == 0 ? Fit::Within : off <= slack ? Fit::Near : off <= 2 * slack ? Fit::Far : Fit::WayOff;
    }

    Taste stomach_range(units::Share tolerance)
    {
        const auto& k = kTuning.enjoyment;
        return { .least = { std::max(0.0f, tolerance.value - k.stomach_reach.value) }, .most = { std::min(1.0f, tolerance.value + k.stomach_margin.value) } };
    }

    i32 enjoyment_points(const RideTaken& ride, const Taste& taste, const Mood& mood)
    {
        const auto& k = kTuning.enjoyment;
        const f32 slack = k.slack.value + k.happy_slack.value * mood.happiness.value;
        const Taste stomach = stomach_range(mood.tolerance);
        const Fit shakes = fit(ride.intensity.value, taste.least.value, taste.most.value, slack);
        const Fit sickens = fit(ride.nausea.value, stomach.least.value, stomach.most.value, slack);
        const auto misses = static_cast<i32>(2 * std::to_underlying(Fit::Within) - std::to_underlying(shakes) - std::to_underlying(sickens));
        i32 points = k.suited - k.per_miss * misses - (shakes == Fit::WayOff || sickens == Fit::WayOff ? k.way_off : 0);

        const f32 dearer = ride.worth.value > 0 ? std::clamp(static_cast<f32>(ride.price.value) / static_cast<f32>(ride.worth.value) - 1, 0.0f, 1.0f) : ride.price.value > 0 ? 1.0f : 0.0f;
        points += static_cast<i32>(std::lround(static_cast<f32>(k.fair_price) + static_cast<f32>(k.dear_price - k.fair_price) * dearer));
        points += std::max(k.longest_wait, k.quick - static_cast<i32>(ride.waited.value / k.per_point.value));
        return points + (ride.new_to_them ? k.new_ride : 0);
    }

    f32 favourite_score(units::Share excitement, i32 points)
    {
        return kTuning.enjoyment.favourite_per_excitement * excitement.value + static_cast<f32>(points);
    }

    bool take_ride(Mood& mood, Favourite& favourite, entt::entity ride, u64 saved, const RideTaken& taken, i32 points)
    {
        const auto& k = kTuning.enjoyment;
        // Judged in the mood they got on in: a ride does not become the favourite of a guest it only cheered up.
        const f32 score = favourite_score(taken.excitement, points);
        const bool has_one = favourite.ride != entt::null || favourite.saved != 0;
        const bool best = mood.happiness.value >= k.content.value && (!has_one || score >= favourite.score);
        mood.happiness.value = clamp01(mood.happiness.value + static_cast<f32>(points) * k.happiness_per_point);
        // Scaled so a guest left middling feels the ride's plain nausea.
        const f32 queasy = std::clamp(1 - mood.happiness.value, k.least_queasy.value, k.most_queasy.value) * 2;
        const f32 stomach = taken.eating ? static_cast<f32>(kTuning.sickness.full_stomach.value) : 1;
        mood.nausea.value = clamp01(mood.nausea.value + taken.nausea.value * kTuning.sickness.ride_nausea * (kTuning.sickness.tolerance_ceiling - mood.tolerance.value) * stomach * queasy);
        if (!best)
            return false;
        favourite = { .ride = ride, .saved = saved, .score = score };
        return true;
    }

    f32 ride_appeal(const RideInMind& ride, const Taste& taste, const Mood& mood)
    {
        const auto& k = kTuning.choosing;
        const f32 known = (ride.favourite ? k.favourite : 0) - (ride.just_been ? k.again : 0);
        if (ride.untested)
            return k.untested + known;
        const f32 slack = kTuning.enjoyment.slack.value + kTuning.enjoyment.happy_slack.value * mood.happiness.value;
        const f32 suits = static_cast<f32>(std::to_underlying(fit(ride.intensity.value, taste.least.value, taste.most.value, slack))) / static_cast<f32>(std::to_underlying(Fit::Within));
        const f32 sickening = std::max(0.0f, ride.nausea.value - stomach_range(mood.tolerance).most.value);
        const f32 bargain = ride.worth.value > 0 ? std::clamp(static_cast<f32>(ride.worth.value - ride.price.value) / static_cast<f32>(ride.worth.value), -1.0f, 1.0f) : 0.0f;
        return k.excitement * ride.excitement.value + k.suits * suits + k.bargain * bargain - k.sickening * sickening + known;
    }

    f32 ride_pull(f32 appeal, f32 near, usize queued)
    {
        return appeal * near * near / (1 + static_cast<f32>(queued) / kTuning.choosing.queue_halves);
    }

} // namespace rct3::simulation
