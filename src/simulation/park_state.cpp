// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park_state.hpp"

#include "simulation/components.hpp"
#include "simulation/tuning.hpp"

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
            }
            s.messages = park_messages(park->messages);
        }
        if (world.weather)
        {
            s.weather = world.weather->type;
            s.forecast = world.weather->forecast;
            s.temperature = world.weather->temperature;
            s.wind = std::clamp(world.weather->wind_speed, 0.0f, 1.0f);
            s.climate = world.weather->climate;
            // The file's time until the next change is in calendar units as ours is.
            s.weather_hold = std::max(world.weather->forecast_delay, 0.0f);
            s.wind_change = std::isfinite(world.weather->wind_acceleration) ? std::clamp(world.weather->wind_acceleration, -1.0f, 1.0f) : 0.0f;
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
        state.date = save::GameClock::date_at(state.time, state.zero_time);
        return static_cast<std::int32_t>(std::max<std::int64_t>(0, week_of(state.time, state.zero_time) - before));
    }

    void rate_week(ParkState& state, const save::PeopleModel& people, const save::RideModel& rides, std::optional<std::int32_t> old_litter)
    {
        RatingInput in = rating_input(people, rides, state.time);
        if (old_litter)
            in.old_litter = *old_litter;
        in.harder = state.harder_rating;
        in.guests = state.guests;
        const RatingParts parts = rate_park(in);
        state.rating_parts = parts;
        state.rating = next_rating(state.rating, parts.total + static_cast<std::int32_t>(state.rating_bonus));
        state.rating_bonus = std::max(0.0f, state.rating_bonus - kTuning.prizes.rating_bonus_fade.value);
    }

    Weather guest_weather(const ParkState& state)
    {
        using enum save::WeatherType;
        // A snowstorm keeps guests in as a thunderstorm does.
        const bool storm = state.weather == Thunderstorm || state.weather == Snowstorm;
        return { .rain = storm || state.weather == LightRain || state.weather == HeavyRain, .storm = storm, .sunny = state.weather == Sunny || state.weather == SunnyCloudy, .temperature = { state.temperature.value_or(Weather { }.temperature.value) } };
    }

} // namespace rct3::simulation
