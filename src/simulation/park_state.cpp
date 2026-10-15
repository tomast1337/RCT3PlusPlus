// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park_state.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::simulation
{

    ParkState park_state(const save::ParkModel* park, const save::WorldModel& world)
    {
        ParkState s;
        if (park != nullptr)
        {
            if (park->has_clock)
            {
                s.time = park->clock.time;
                s.zero_time = park->clock.zero_time;
                s.date = park->clock.date();
            }
            if (park->has_finance)
            {
                if (!park->finance.unlimited())
                    s.cash = park->finance.cash;
                s.loan = park->finance.loan;
            }
            if (park->has_park)
            {
                if (const auto guests = park->park.guests_in_park.latest())
                    s.guests = static_cast<std::int32_t>(std::lround(*guests));
                if (const auto rating = park->park.rating())
                    s.rating = *rating / save::kMaxParkRating;
                s.harder_rating = park->park.flags.has(save::ParkFlag::HarderRating);
                s.rating_carry.penalty = park->park.rating_penalty;
            }
            s.messages = park_messages(park->messages);
        }
        if (world.weather)
        {
            s.weather = world.weather->type;
            s.forecast = world.weather->forecast;
            s.temperature = world.weather->temperature;
        }
        return s;
    }

    namespace
    {
        std::int64_t week_of(float time, float zero_time) noexcept
        {
            return static_cast<std::int64_t>(std::floor((time - zero_time) / save::GameClock::kUnitsPerWeek));
        }
    } // namespace

    std::int32_t pass_time(ParkState& state, float seconds)
    {
        if (seconds <= 0)
            return 0;
        const std::int64_t before = week_of(state.time, state.zero_time);
        state.time += seconds / kSecondsPerHour * kCalendarUnitsPerHour;
        state.date = save::GameClock::split(state.time);
        return static_cast<std::int32_t>(std::max<std::int64_t>(0, week_of(state.time, state.zero_time) - before));
    }

    void rate_week(ParkState& state, const save::PeopleModel& people, const save::RideModel& rides)
    {
        RatingInput in = rating_input(people, rides, state.time);
        in.harder = state.harder_rating;
        in.guests = state.guests;
        in.penalty = state.rating_carry.penalty;
        in.bonus = state.rating_carry.bonus;
        const RatingParts parts = rate_park(in);
        state.rating_parts = parts;
        state.rating = static_cast<float>(parts.total) / static_cast<float>(kMaxRating);
        state.rating_carry = fade(state.rating_carry, !state.cash.has_value());
    }

} // namespace rct3::simulation
