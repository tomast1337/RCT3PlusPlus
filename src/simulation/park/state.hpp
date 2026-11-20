// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park as it runs: the values the game keeps going while the park is shown, started from the saved park.
// The interface reads them from here instead of from the save, so what it shows is what the park is at now.
// Today the game speed (by the player's hand), the calendar, the messages' ages and the weekly rating change;
// the simulation will move the rest.
#pragma once

#include "core/types.hpp"
#include "save/model/park.hpp"
#include "save/model/world.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/game_speed.hpp"
#include "simulation/park/messages.hpp"
#include "simulation/park/rating.hpp"
#include "units/units.hpp"

#include <cstdint>
#include <optional>

namespace rct3::simulation
{
    struct Weather;
} // namespace rct3::simulation

namespace rct3::simulation::park
{
    struct ParkState
    {
        std::optional<units::Money> cash; ///< None when money never runs out
        units::Money loan { };
        i32 guests = 0;                          ///< In the park now
        std::optional<f32> rating;               ///< 0 to 1; none before the park has one
        std::optional<RatingParts> rating_parts; ///< How the rating was made up; none until a week has been rated
        bool harder_rating = false;              ///< The scenario asks for a harder rating
        /// Points a prize adds to each week's score, out of 1000, fading week by week.
        /// shortcut: not kept in the park file, so a park saved and opened again loses what is left of it.
        f32 rating_bonus = 0;
        f32 time = 0;                   ///< Calendar units since the park's calendar began, as the park file keeps them
        f32 zero_time = 0;              ///< Where the calendar's weeks are counted from
        save::model::CalendarDate date; ///< The calendar's date at `time`
        save::model::WeatherType weather = save::model::WeatherType::Unknown;
        std::optional<save::model::WeatherType> forecast; ///< The weather on its way
        std::optional<units::Degrees> temperature;
        f32 wind = 0;         ///< How strong the wind blows, 0 calm to 1 strong
        i32 climate = 0;      ///< The park file's climate (simulation::Climate)
        f32 weather_hold = 0; ///< Calendar units before the forecast arrives
        f32 wind_change = 0;  ///< How fast the wind is rising (falling when negative)
        f32 wind_wait = 0;    ///< Calendar units to the wind's next change; not saved
        GameSpeed speed;
        ParkMessages messages;
        bool cheats_used = false; ///< A cheat was used in the park at some time; kept for good, cheats off or on
    };

    /// Calendar units a game hour moves the calendar on: a whole day of the clock is a month and three fifths
    /// of the calendar. The calendar runs on while the clock skips the hours the park is shut.
    inline constexpr f32 kCalendarUnitsPerHour = 24.0f;

    /// The state a saved park starts in. `park` is null when the park's model could not be read.
    [[nodiscard]] ParkState park_state(const save::model::ParkModel* park, const save::model::WorldModel& world);

    /// The weather the simulated guests feel, from the park's.
    [[nodiscard]] Weather guest_weather(const ParkState& state);

    /// Moves the calendar on by `seconds` of game time, at the clock's rate of real seconds per hour. Returns
    /// how many new calendar weeks were started.
    i32 pass_time(ParkState& state, f32 seconds);

    /// Scores the park for a new week from `input` (see rating_input), and moves the rating towards the score.
    /// The guest count, the harder flag and the prize come from the state.
    void rate_week(ParkState& state, RatingInput input);

} // namespace rct3::simulation::park
