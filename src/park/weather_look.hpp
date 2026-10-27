// SPDX-License-Identifier: GPL-3.0-or-later
//
// How the weather looks on screen: how hard the rain falls, how grey the day turns, and the lightning of a
// storm with its thunder after. The look eases from one kind of weather to the next over a few seconds, so a
// change does not jump. It runs on real seconds and its own numbers: it is never saved and never changes how
// the park plays.
#pragma once

#include "random/generator.hpp"
#include "save/model/world.hpp"

#include <glm/vec3.hpp>

namespace rct3::park
{

    struct WeatherLook
    {
        float rain = 0;        ///< 0 dry to 1 a downpour
        float gloom = 0;       ///< 0 clear to 1 as dark as a storm
        float flash = 0;       ///< The lightning's brightness now, 0 to 1
        float to_flash = 0;    ///< Seconds to the next stroke while it storms
        float to_thunder = -1; ///< Seconds until the last stroke is heard; below 0 when none is coming
        float flicker = -1;    ///< Seconds to a stroke's second flicker; below 0 when none is coming
    };

    /// What weather `type` looks like once the look has settled.
    struct WeatherAim
    {
        float rain = 0;
        float gloom = 0;
        bool lightning = false;
    };
    [[nodiscard]] WeatherAim weather_aim(save::WeatherType type);

    /// Moves the look `seconds` on towards weather `type`. True when a stroke's thunder is to be heard now.
    bool advance_weather_look(WeatherLook& look, save::WeatherType type, float seconds, random::RandomState& dice);

    /// Light of `colour` as the look leaves it: greyer and dimmer the gloomier, whiter in a flash. `strength`
    /// is how much the gloom takes from it, 1 for direct sunlight that the clouds hide most.
    [[nodiscard]] glm::vec3 weathered(glm::vec3 colour, const WeatherLook& look, float strength);

} // namespace rct3::park
