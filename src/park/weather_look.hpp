// SPDX-License-Identifier: GPL-3.0-or-later
//
// How the weather looks on screen: how hard the rain falls, how grey the day turns, and the lightning of a
// storm with its thunder after. The look eases from one kind of weather to the next over a few seconds, so a
// change does not jump. It runs on real seconds and its own numbers: it is never saved and never changes how
// the park plays.
#pragma once

#include "random/generator.hpp"
#include "save/model/world.hpp"
#include "units/units.hpp"

#include <glm/vec3.hpp>
#include <optional>

namespace rct3::park
{

    struct WeatherLook
    {
        float rain = 0;                               ///< 0 dry to 1 a downpour
        float gloom = 0;                              ///< 0 clear to 1 as dark as a storm
        float flash = 0;                              ///< The lightning's brightness now, 0 to 1
        units::Seconds to_flash { };                  ///< To the next stroke while it storms
        std::optional<units::Seconds> to_thunder { }; ///< Until the last stroke is heard; none when none is coming
        std::optional<units::Seconds> flicker { };    ///< To a stroke's second flicker; none when none is coming
    };

    /// What weather `type` looks like once the look has settled.
    struct WeatherAim
    {
        float rain = 0;
        float gloom = 0;
        bool lightning = false;
    };
    [[nodiscard]] WeatherAim weather_aim(save::model::WeatherType type);

    /// Moves the look `seconds` on towards weather `type`. True when a stroke's thunder is to be heard now.
    bool advance_weather_look(WeatherLook& look, save::model::WeatherType type, float seconds, random::RandomState& dice);

    /// Light of `colour` as the look leaves it: greyer and dimmer the gloomier, whiter in a flash. `strength`
    /// is how much the gloom takes from it, 1 for direct sunlight that the clouds hide most.
    [[nodiscard]] glm::vec3 weathered(glm::vec3 colour, const WeatherLook& look, float strength);

} // namespace rct3::park
