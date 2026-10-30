// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park as it runs: the values the game keeps going while the park is shown, started from the saved park.
// The interface reads them from here instead of from the save, so what it shows is what the park is at now.
// Today the game speed (by the player's hand), the calendar, the messages' ages and the weekly rating change;
// the simulation will move the rest.
#pragma once

#include "save/model/park.hpp"
#include "save/model/world.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/game_speed.hpp"
#include "simulation/park_messages.hpp"
#include "simulation/park_rating.hpp"

#include <cstdint>
#include <optional>

namespace rct3::simulation
{
    struct Weather;

    struct ParkState
    {
        std::optional<std::int64_t> cash;        ///< Hundredths of the currency unit; none when money never runs out
        std::int64_t loan = 0;                   ///< Hundredths of the currency unit
        std::int32_t guests = 0;                 ///< In the park now
        std::optional<float> rating;             ///< 0 to 1; none before the park has one
        std::optional<RatingParts> rating_parts; ///< How the rating was made up; none until a week has been rated
        bool harder_rating = false;              ///< The scenario asks for a harder rating
        /// Points a prize adds to each week's score, out of 1000, fading week by week.
        /// shortcut: not kept in the park file, so a park saved and opened again loses what is left of it.
        float rating_bonus = 0;
        float time = 0;          ///< Calendar units since the park's calendar began, as the park file keeps them
        float zero_time = 0;     ///< Where the calendar's weeks are counted from
        save::CalendarDate date; ///< The calendar's date at `time`
        save::WeatherType weather = save::WeatherType::Unknown;
        std::optional<save::WeatherType> forecast; ///< The weather on its way
        std::optional<float> temperature;          ///< Degrees Celsius
        float wind = 0;                            ///< How strong the wind blows, 0 calm to 1 strong
        std::int32_t climate = 0;                  ///< The park file's climate (simulation::Climate)
        float weather_hold = 0;                    ///< Calendar units before the forecast arrives
        float wind_change = 0;                     ///< How fast the wind is rising (falling when negative)
        float wind_wait = 0;                       ///< Calendar units to the wind's next change; not saved
        GameSpeed speed;
        ParkMessages messages;
    };

    /// Calendar units a game hour moves the calendar on: a whole day of the clock is a month and three fifths
    /// of the calendar. The calendar runs on while the clock skips the hours the park is shut.
    inline constexpr float kCalendarUnitsPerHour = 24.0f;

    /// The state a saved park starts in. `park` is null when the park's model could not be read.
    [[nodiscard]] ParkState park_state(const save::ParkModel* park, const save::WorldModel& world);

    /// The weather the simulated guests feel, from the park's.
    [[nodiscard]] Weather guest_weather(const ParkState& state);

    /// Moves the calendar on by `seconds` of game time, at the clock's rate of real seconds per hour. Returns
    /// how many new calendar weeks were started.
    std::int32_t pass_time(ParkState& state, float seconds);

    /// Scores the park for a new week, from its people and rides as they stand, and moves the rating towards
    /// the score.
    /// `old_litter`, when given, counts the litter lying long enough to count instead of the saved litter.
    void rate_week(ParkState& state, const save::PeopleModel& people, const save::RideModel& rides, std::optional<std::int32_t> old_litter = std::nullopt);

} // namespace rct3::simulation
