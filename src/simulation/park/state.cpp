// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park/state.hpp"

#include "core/types.hpp"
#include "simulation/components.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::simulation::park
{

    ParkState park_state(const save::model::ParkModel* park, const save::model::WorldModel& world)
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
                    s.cash = units::Money { park->finance.cash };
                s.loan = { park->finance.loan };
            }
            if (park->has_park)
            {
                if (const auto guests = park->park.guests_in_park.latest())
                    s.guests = static_cast<i32>(std::lround(*guests));
                if (const auto rating = park->park.rating())
                    s.rating = *rating / save::model::kMaxParkRating;
                s.harder_rating = park->park.flags.has(save::model::ParkFlag::HarderRating);
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
        i64 week_of(f32 time, f32 zero_time) noexcept
        {
            return static_cast<i64>(std::floor((time - zero_time) / save::model::GameClock::kUnitsPerWeek));
        }
    } // namespace

    i32 pass_time(ParkState& state, f32 seconds)
    {
        if (seconds <= 0)
            return 0;
        const i64 before = week_of(state.time, state.zero_time);
        state.time += seconds / kSecondsPerHour * kCalendarUnitsPerHour;
        state.date = save::model::GameClock::date_at(state.time, state.zero_time);
        return static_cast<i32>(std::max<i64>(0, week_of(state.time, state.zero_time) - before));
    }

    void rate_week(ParkState& state, RatingInput input)
    {
        input.harder = state.harder_rating;
        input.guests = state.guests;
        input.prize = state.rating_bonus;
        const RatingParts parts = rate_park(input);
        state.rating_parts = parts;
        state.rating = next_rating(state.rating, parts.total);
        state.rating_bonus = std::max(0.0f, state.rating_bonus - kTuning.prizes.rating_bonus_fade.value);
    }

    Weather guest_weather(const ParkState& state)
    {
        using enum save::model::WeatherType;
        // A snowstorm keeps guests in as a thunderstorm does.
        const bool storm = state.weather == Thunderstorm || state.weather == Snowstorm;
        return { .rain = storm || state.weather == LightRain || state.weather == HeavyRain, .storm = storm, .sunny = state.weather == Sunny || state.weather == SunnyCloudy, .temperature = state.temperature.value_or(Weather { }.temperature) };
    }

} // namespace rct3::simulation::park
