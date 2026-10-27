// SPDX-License-Identifier: GPL-3.0-or-later
//
// The weather as it moves on by itself. Each park has a climate, kept in its save; for every month of the
// season the climate says how warm it is and how likely each kind of weather is. The coming weather, the
// forecast, is rolled ahead: the weather now holds for a while, then the forecast arrives and the next one is
// rolled. The temperature eases towards what the weather and the month make it, and the wind wanders,
// stronger in bad weather. Snow never comes by itself.
//
// The climates' warmth and odds, how long weather holds and how fast things change are chosen by eye.
#pragma once

#include "random/generator.hpp"
#include "save/model/world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace rct3::simulation
{

    struct ParkState;

    /// The climates a park file can name, in the order of the scenario editor's list.
    enum class Climate : std::int32_t
    {
        Temperate,
        Spooky,
        Plains,
        Martian,
        Winter,
        Desert,
        TropicalIsland,
        MountainRange,
        AlpineForest,
        Jungle,
        Count_,
    };

    /// The kinds the weather moves between by itself: sunny to thunderstorm, by their save numbers.
    inline constexpr std::size_t kRolledWeathers = 6;

    /// What a climate is like in one month.
    struct ClimateMonth
    {
        float warmth = 0;                             ///< Degrees Celsius on a cloudy day
        std::array<double, kRolledWeathers> odds { }; ///< Of each kind, sunny first; they add up to 1
    };

    /// Climate `climate` (a park file's number; an unknown one is temperate) in month `month`, 0 March to 7
    /// October (other numbers wrap).
    [[nodiscard]] ClimateMonth climate_month(std::int32_t climate, std::int32_t month);

    /// The temperature weather `type` brings in month `month` of `climate`.
    [[nodiscard]] float weather_temperature(std::int32_t climate, std::int32_t month, save::WeatherType type);

    /// The climate the original game's career scenario `scenario` (by its id) is played in; none for any other
    /// park, which keeps its file's.
    [[nodiscard]] std::optional<std::int32_t> career_climate(std::string_view scenario);

    /// Moves the weather on by `units` of calendar time, rolling forecasts with `dice`. Nothing changes while
    /// `frozen` (the Disable weather option).
    void advance_weather(ParkState& state, float units, random::RandomState& dice, bool frozen);

} // namespace rct3::simulation
