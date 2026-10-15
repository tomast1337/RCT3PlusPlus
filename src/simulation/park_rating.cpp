// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park_rating.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::simulation
{

    namespace
    {
        constexpr std::int32_t kHarderBase = -100;
        constexpr std::int32_t kMaxGuestsPart = 150;
        constexpr float kGuestsForMax = 2000.0f;
        constexpr std::int32_t kEmptyParkHappy = 250;
        constexpr std::int32_t kHappyScale = 600;
        constexpr std::int32_t kMaxHappyPart = 500;
        constexpr float kHappyAbove = 0.5f;
        constexpr float kUpTimeScale = 2.0f;
        constexpr std::size_t kFullRideCount = 10; ///< Fewer rides scale the up-time part down
        constexpr float kScorePerPoint = 1.25f;
        constexpr std::int32_t kMaxScorePart = 100;
        constexpr float kNiceExcitement = 3.7f;
        constexpr float kNiceIntensity = 5.2f;
        constexpr float kNiceMost = 50.0f;
        constexpr float kNiceFalloff = 6.25f; ///< Taken off per point the average is away from nice
        constexpr std::int32_t kPerLitter = 4;
        constexpr std::int32_t kMaxLitterPart = 600;
        constexpr std::int32_t kPenaltyFade = 49;
        constexpr std::int32_t kUnlimitedPenaltyFade = 280;
        constexpr std::int32_t kBonusFade = 3;
        constexpr std::uint32_t kLitterNotCounted = 1u << 5;

        /// To the nearest whole number, halves to even.
        std::int32_t whole(float v) noexcept
        {
            return static_cast<std::int32_t>(std::nearbyint(v));
        }

        /// Scores one of the two ride measures: its total, capped, plus a share for an average near `nice`.
        std::int32_t measure_part(float sum, std::size_t count, float nice) noexcept
        {
            std::int32_t part = std::min(kMaxScorePart, whole(sum * kScorePerPoint));
            if (count > 0)
            {
                const float average = sum / static_cast<float>(count);
                part += std::max(0, whole(kNiceMost - std::abs(average - nice) * kNiceFalloff));
            }
            return part;
        }
    } // namespace

    RatingParts rate_park(const RatingInput& input)
    {
        RatingParts p;
        p.base = input.harder ? kHarderBase : 0;
        p.guests = std::min(kMaxGuestsPart, whole(static_cast<float>(std::max(input.guests, 0)) * kMaxGuestsPart / kGuestsForMax));
        if (input.guests <= 0)
            p.happy_guests = kEmptyParkHappy;
        else if (input.guests_inside > 0)
            p.happy_guests = std::min(kMaxHappyPart, input.happy_guests * kHappyScale / input.guests_inside);

        if (!input.rides.empty())
        {
            float up = 0;
            for (const RideRatingInput& ride : input.rides)
                up += static_cast<float>(whole(100.0f - ride.downtime_percent));
            const auto count = input.rides.size();
            up = up * kUpTimeScale / static_cast<float>(count);
            if (count < kFullRideCount)
                up *= static_cast<float>(count) / static_cast<float>(kFullRideCount);
            p.ride_up_time = whole(up);
        }

        float excitement = 0;
        float intensity = 0;
        std::size_t rated = 0;
        for (const RideRatingInput& ride : input.rides)
        {
            if (!ride.excitement || !ride.intensity)
                continue;
            excitement += *ride.excitement;
            intensity += *ride.intensity;
            ++rated;
        }
        p.ride_scores = measure_part(excitement, rated, kNiceExcitement) + measure_part(intensity, rated, kNiceIntensity);

        p.litter = -std::min(kMaxLitterPart, kPerLitter * std::max(input.old_litter, 0));
        p.penalty = -std::max(input.penalty, 0);
        p.bonus = std::max(input.bonus, 0);
        const std::int32_t sum = p.base + p.guests + p.happy_guests + p.ride_up_time + p.ride_scores + p.litter + p.penalty + p.bonus;
        p.total = std::clamp(sum, 0, kMaxRating);
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
            if (guest.mood_now.happiness > kHappyAbove)
                ++in.happy_guests;
        }
        for (const save::Ride& ride : rides.rides)
        {
            // Only rides that carry guests on a track or a moving ride; facilities and exhibits never count.
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
        in.old_litter = static_cast<std::int32_t>(std::ranges::count_if(people.litter, [before](const save::Litter& l) { return l.create_time < before && (l.flags.value_or(0) & kLitterNotCounted) == 0; }));
        return in;
    }

    RatingCarry fade(RatingCarry carry, bool unlimited_money) noexcept
    {
        carry.penalty = std::max(0, carry.penalty - (unlimited_money ? kUnlimitedPenaltyFade : kPenaltyFade));
        carry.bonus = std::max(0, carry.bonus - kBonusFade);
        return carry;
    }

} // namespace rct3::simulation
