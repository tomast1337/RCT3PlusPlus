// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/enjoyment.hpp"

#include "simulation/tuning.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rct3::simulation
{

    Fit fit(float rating, float least, float most, float slack)
    {
        const float off = std::max({ least - rating, rating - most, 0.0f });
        return off == 0 ? Fit::Within : off <= slack ? Fit::Near : off <= 2 * slack ? Fit::Far : Fit::WayOff;
    }

    Taste stomach_range(units::Share tolerance)
    {
        const auto& k = kTuning.enjoyment;
        return { .least = { std::max(0.0f, tolerance.value - k.stomach_reach.value) }, .most = { std::min(1.0f, tolerance.value + k.stomach_margin.value) } };
    }

    std::int32_t enjoyment_points(const RideTaken& ride, const Taste& taste, const Mood& mood)
    {
        const auto& k = kTuning.enjoyment;
        const float slack = k.slack.value + k.happy_slack.value * mood.happiness.value;
        const Taste stomach = stomach_range(mood.tolerance);
        const Fit shakes = fit(ride.intensity.value, taste.least.value, taste.most.value, slack);
        const Fit sickens = fit(ride.nausea.value, stomach.least.value, stomach.most.value, slack);
        const auto misses = static_cast<std::int32_t>(2 * std::to_underlying(Fit::Within) - std::to_underlying(shakes) - std::to_underlying(sickens));
        std::int32_t points = k.suited - k.per_miss * misses - (shakes == Fit::WayOff || sickens == Fit::WayOff ? k.way_off : 0);

        const float dearer = ride.worth.value > 0 ? std::clamp(static_cast<float>(ride.price.value) / static_cast<float>(ride.worth.value) - 1, 0.0f, 1.0f) : ride.price.value > 0 ? 1.0f : 0.0f;
        points += static_cast<std::int32_t>(std::lround(static_cast<float>(k.fair_price) + static_cast<float>(k.dear_price - k.fair_price) * dearer));
        points += std::max(k.longest_wait, k.quick - static_cast<std::int32_t>(ride.waited.value / k.per_point.value));
        return points + (ride.new_to_them ? k.new_ride : 0);
    }

    float favourite_score(units::Share excitement, std::int32_t points)
    {
        return kTuning.enjoyment.favourite_per_excitement * excitement.value + static_cast<float>(points);
    }

    bool take_ride(Mood& mood, Favourite& favourite, entt::entity ride, std::uint64_t saved, const RideTaken& taken, std::int32_t points)
    {
        const auto& k = kTuning.enjoyment;
        mood.happiness.value = clamp01(mood.happiness.value + static_cast<float>(points) * k.happiness_per_point);
        // Scaled so a guest left middling feels the ride's plain nausea.
        const float queasy = std::clamp(1 - mood.happiness.value, k.least_queasy.value, k.most_queasy.value) * 2;
        const float stomach = taken.eating ? static_cast<float>(kTuning.sickness.full_stomach.value) : 1;
        mood.nausea.value = clamp01(mood.nausea.value + taken.nausea.value * kTuning.sickness.ride_nausea * (kTuning.sickness.tolerance_ceiling - mood.tolerance.value) * stomach * queasy);
        const float score = favourite_score(taken.excitement, points);
        const bool has_one = favourite.ride != entt::null || favourite.saved != 0;
        if (mood.happiness.value < k.content.value || (has_one && score < favourite.score))
            return false;
        favourite = { .ride = ride, .saved = saved, .score = score };
        return true;
    }

} // namespace rct3::simulation
