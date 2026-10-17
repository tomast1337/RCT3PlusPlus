// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park_rating.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::simulation
{

    namespace
    {
        // Our own values: each part's share of the 1000, and the shapes inside them.
        constexpr float kHappinessShare = 500;
        constexpr float kRidesShare = 300;
        constexpr float kReliabilityShare = 100;
        constexpr float kGuestsShare = 300;
        constexpr float kEmptyParkHappiness = 0.5f;
        constexpr float kGoodExcitement = 7;  ///< A ride this exciting scores in full
        constexpr float kHarshIntensity = 7;  ///< Above this, a ride starts to put guests off
        constexpr float kIntensityRange = 4;  ///< How far above harsh a ride stops scoring at all
        constexpr float kRidesForVariety = 3; ///< About two thirds of the variety at this many rides
        constexpr float kGuestsForBusy = 300; ///< About two thirds of the guests part at this many guests
        constexpr std::int32_t kPerLitter = 3;
        constexpr std::int32_t kMostLitter = 500;
        constexpr float kHarderShare = 0.15f; ///< Taken off the score by the harder rating
        constexpr float kWeeklyMove = 0.5f;   ///< Share of the way the rating moves towards the week's score

        std::int32_t whole(float v) noexcept
        {
            return static_cast<std::int32_t>(std::lround(v));
        }

        /// 0..1: exciting is good, too intense is not.
        float appeal(float excitement, float intensity) noexcept
        {
            const float thrill = std::clamp(excitement / kGoodExcitement, 0.0f, 1.0f);
            const float comfort = std::clamp(1.0f - (intensity - kHarshIntensity) / kIntensityRange, 0.0f, 1.0f);
            return thrill * comfort;
        }
    } // namespace

    RatingParts rate_park(const RatingInput& input)
    {
        RatingParts p;
        const float happiness = input.guests_inside > 0 ? input.happiness / static_cast<float>(input.guests_inside) : kEmptyParkHappiness;
        p.happiness = whole(kHappinessShare * std::clamp(happiness, 0.0f, 1.0f));

        float appeal_sum = 0;
        float up_sum = 0;
        std::size_t tested = 0;
        for (const RideRatingInput& ride : input.rides)
        {
            up_sum += std::clamp(1.0f - ride.downtime_percent / 100.0f, 0.0f, 1.0f);
            if (ride.excitement && ride.intensity)
            {
                appeal_sum += appeal(*ride.excitement, *ride.intensity);
                ++tested;
            }
        }
        const auto count = static_cast<float>(input.rides.size());
        if (tested > 0)
            p.rides = whole(kRidesShare * appeal_sum / static_cast<float>(tested) * (1.0f - std::exp(-count / kRidesForVariety)));
        if (!input.rides.empty())
            p.reliability = whole(kReliabilityShare * up_sum / count);

        p.guests = whole(kGuestsShare * (1.0f - std::exp(-static_cast<float>(std::max(input.guests, 0)) / kGuestsForBusy)));
        p.litter = -std::min(kMostLitter, kPerLitter * std::max(input.old_litter, 0));

        const std::int32_t sum = p.happiness + p.rides + p.reliability + p.guests + p.litter;
        if (input.harder)
            p.harder = -whole(static_cast<float>(std::max(sum, 0)) * kHarderShare);
        p.total = std::clamp(sum + p.harder, 0, kMaxRating);
        return p;
    }

    RatingInput rating_input(const save::PeopleModel& people, const save::RideModel& rides, float now)
    {
        RatingInput in;
        for (const save::Guest& guest : people.guests)
        {
            if (!guest.in_park)
                continue;
            ++in.guests_inside;
            in.happiness += std::clamp(guest.mood_now.happiness, 0.0f, 1.0f);
        }
        for (const save::Ride& ride : rides.rides)
        {
            // Only rides guests ride; facilities and exhibits never count.
            if (ride.kind == save::RideKind::Special)
                continue;
            RideRatingInput r;
            r.downtime_percent = ride.breakdown.downtime_percent();
            if (ride.assessment.valid)
            {
                r.excitement = ride.assessment.excitement;
                r.intensity = ride.assessment.intensity;
            }
            in.rides.push_back(r);
        }
        const float before = now - kLitterCountsAfter;
        in.old_litter = static_cast<std::int32_t>(std::ranges::count_if(people.litter, [before](const save::Litter& l) { return l.create_time < before; }));
        return in;
    }

    float next_rating(std::optional<float> before, std::int32_t score) noexcept
    {
        const float target = static_cast<float>(std::clamp(score, 0, kMaxRating)) / static_cast<float>(kMaxRating);
        return before ? *before + (target - *before) * kWeeklyMove : target;
    }

} // namespace rct3::simulation
