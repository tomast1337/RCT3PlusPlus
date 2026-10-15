// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park as it runs: the values the game keeps going while the park is shown, started from the saved park.
// The interface reads them from here instead of from the save, so what it shows is what the park is at now.
// Today only the game speed (by the player's hand), the calendar and the messages' ages change; the simulation
// will move the rest.
#pragma once

#include "save/model/park.hpp"
#include "save/model/world.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/game_speed.hpp"
#include "simulation/park_messages.hpp"

#include <cstdint>
#include <optional>

namespace rct3::simulation
{

    struct ParkState
    {
        std::optional<std::int64_t> cash; ///< Hundredths of the currency unit; none when money never runs out
        std::int64_t loan = 0;            ///< Hundredths of the currency unit
        std::int32_t guests = 0;          ///< In the park now
        std::optional<float> rating;      ///< 0 to 1; none before the park has one
        float time = 0;                   ///< Calendar units since the park's calendar began, as the park file keeps them
        save::CalendarDate date;          ///< The calendar's date at `time`
        save::WeatherType weather = save::WeatherType::Unknown;
        std::optional<save::WeatherType> forecast; ///< The weather on its way
        std::optional<float> temperature;          ///< Degrees Celsius
        GameSpeed speed;
        ParkMessages messages;
    };

    /// Calendar units a game hour moves the calendar on: a whole day of the clock is a month and three fifths
    /// of the calendar. The calendar runs on while the clock skips the hours the park is shut.
    inline constexpr float kCalendarUnitsPerHour = 24.0f;

    /// The state a saved park starts in. `park` is null when the park's model could not be read.
    [[nodiscard]] ParkState park_state(const save::ParkModel* park, const save::WorldModel& world);

    /// Moves the calendar on by `seconds` of game time, at the clock's rate of real seconds per hour.
    void pass_time(ParkState& state, float seconds);

} // namespace rct3::simulation
