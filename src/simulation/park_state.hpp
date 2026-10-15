// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park as it runs: the values the game keeps going while the park is shown, started from the saved park.
// The interface reads them from here instead of from the save, so what it shows is what the park is at now.
// Today only the game speed changes, by the player's hand; the simulation will move the rest.
#pragma once

#include "save/model/park.hpp"
#include "save/model/world.hpp"
#include "simulation/game_speed.hpp"

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
        save::CalendarDate date;
        save::WeatherType weather = save::WeatherType::Unknown;
        std::optional<save::WeatherType> forecast; ///< The weather on its way
        std::optional<float> temperature;          ///< Degrees Celsius
        GameSpeed speed;
    };

    /// The state a saved park starts in. `park` is null when the park's model could not be read.
    [[nodiscard]] ParkState park_state(const save::ParkModel* park, const save::WorldModel& world);

} // namespace rct3::simulation
