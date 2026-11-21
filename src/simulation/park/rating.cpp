// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park/rating.hpp"

#include "core/types.hpp"
#include "simulation/awards.hpp"
#include "simulation/park/state.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::simulation::park
{

    namespace
    {
        /// How long a piece of litter must lie, in calendar units, before it counts against the rating: as long
        /// as the running park counts it after.
        constexpr f32 kLitterCountsAfter = kTuning.litter.old.value / kSecondsPerHour * kCalendarUnitsPerHour;

        i32 whole(f32 v) noexcept
        {
            return static_cast<i32>(std::lround(v));
        }

        /// 0..1: exciting is good, too intense is not.
        f32 appeal(f32 excitement, f32 intensity) noexcept
        {
            const f32 thrill = std::clamp(excitement / kTuning.rating.good_excitement.value, 0.0f, 1.0f);
            const f32 comfort = std::clamp(1.0f - (intensity - kTuning.rating.harsh_intensity.value) / kTuning.rating.intensity_range.value, 0.0f, 1.0f);
            return thrill * comfort;
        }
    } // namespace

    RatingParts rate_park(const RatingInput& input)
    {
        RatingParts p;
        const f32 happiness = input.guests_inside > 0 ? input.happiness / static_cast<f32>(input.guests_inside) : kTuning.rating.empty_park_happiness.value;
        p.happiness = whole(kTuning.rating.happiness_share.value * std::clamp(happiness, 0.0f, 1.0f));

        f32 appeal_sum = 0;
        f32 up_sum = 0;
        usize tested = 0;
        for (const RideRatingInput& ride : input.rides)
        {
            up_sum += std::clamp(1.0f - ride.downtime_percent / 100.0f, 0.0f, 1.0f);
            if (ride.excitement && ride.intensity)
            {
                appeal_sum += appeal(*ride.excitement, *ride.intensity);
                ++tested;
            }
        }
        const auto count = static_cast<f32>(input.rides.size());
        if (tested > 0)
            p.rides = whole(kTuning.rating.rides_share.value * appeal_sum / static_cast<f32>(tested) * (1.0f - std::exp(-count / kTuning.rating.rides_for_variety)));
        if (!input.rides.empty())
            p.reliability = whole(kTuning.rating.reliability_share.value * up_sum / count);

        p.guests = whole(kTuning.rating.guests_share.value * (1.0f - std::exp(-static_cast<f32>(std::max(input.guests, 0)) / kTuning.rating.guests_for_busy)));
        const auto& t = kTuning.rating;
        p.litter = -std::min(t.most_litter, t.per_litter * std::max(input.old_litter, 0));
        p.lost_guests = -std::min(t.most_lost_guests, t.per_lost_guest * std::max(input.lost_guests, 0));
        p.awards = whole(t.per_award.value * static_cast<f32>(input.awards) - t.per_fine.value * static_cast<f32>(input.fines));
        p.prize = whole(std::max(input.prize, 0.0f));

        const i32 sum = p.happiness + p.rides + p.reliability + p.guests + p.litter + p.lost_guests + p.awards;
        if (input.harder)
            p.harder = -whole(static_cast<f32>(std::max(sum, 0)) * t.harder_share.value);
        // The harder rating leaves a prize whole.
        p.total = std::clamp(sum + p.harder + p.prize, 0, kMaxRating);
        return p;
    }

    RatingInput rating_input(const save::model::PeopleModel& people, const save::model::RideModel& rides, const save::model::Awards& awards, f32 now)
    {
        RatingInput in;
        for (const save::model::Guest& guest : people.guests)
        {
            if (!guest.in_park)
                continue;
            ++in.guests_inside;
            in.lost_guests += guest.person.state == save::model::PersonState::Lost ? 1 : 0;
            in.happiness += std::clamp(guest.mood_now.happiness, 0.0f, 1.0f);
        }
        for (const save::model::Ride& ride : rides.rides)
        {
            // Only rides guests ride; facilities and exhibits never count.
            if (ride.kind == save::model::RideKind::Special || ride.base.demolished)
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
        for (const auto& month : awards.earned)
            for (const i32 kind : month)
                ++(simulation::award_is_fine(kind) ? in.fines : in.awards);
        const f32 before = now - kLitterCountsAfter;
        in.old_litter = static_cast<i32>(std::ranges::count_if(people.litter, [before](const save::model::Litter& l) { return l.create_time < before; }));
        return in;
    }

    f32 next_rating(std::optional<f32> before, i32 score) noexcept
    {
        const f32 target = static_cast<f32>(std::clamp(score, 0, kMaxRating)) / static_cast<f32>(kMaxRating);
        return before ? *before + (target - *before) * kTuning.rating.weekly_move.value : target;
    }

} // namespace rct3::simulation::park
