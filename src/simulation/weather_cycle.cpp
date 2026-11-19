// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/weather_cycle.hpp"

#include "core/types.hpp"
#include "simulation/park/state.hpp"
#include "simulation/random_events.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace rct3::simulation
{

    namespace
    {
        constexpr usize kMonths = 8;

        /// A climate through the season, March to October.
        struct ClimateShape
        {
            std::array<units::Degrees, kMonths> warmth; ///< On a cloudy day
            std::array<f32, kMonths> wet;               ///< Share of rolls that rain
            f32 storm = 0;                              ///< Share of the rain that comes as thunderstorms
            f32 cloud = 0;                              ///< Share of the dry weather that is not plain sunny
        };

        // Chosen by eye, each from the name of its climate: deserts and the red planet are dry and hot, the
        // tropics and the jungle wet in high summer, spooky parks often stormy, winter parks cold.
        constexpr std::array<ClimateShape, static_cast<usize>(Climate::Count_)> kClimates { {
            { .warmth = { 12, 15, 19, 23, 25, 24, 19, 14 }, .wet = { 0.4f, 0.35f, 0.3f, 0.2f, 0.2f, 0.25f, 0.35f, 0.45f }, .storm = 0.15f, .cloud = 0.5f },  // Temperate
            { .warmth = { 10, 12, 15, 17, 17, 15, 12, 9 }, .wet = { 0.5f, 0.5f, 0.45f, 0.4f, 0.4f, 0.45f, 0.55f, 0.6f }, .storm = 0.5f, .cloud = 0.8f },     // Spooky
            { .warmth = { 13, 17, 22, 27, 29, 27, 21, 15 }, .wet = { 0.25f, 0.25f, 0.2f, 0.12f, 0.12f, 0.15f, 0.25f, 0.3f }, .storm = 0.3f, .cloud = 0.3f }, // Plains
            { .warmth = { 30, 33, 36, 39, 40, 38, 34, 31 }, .wet = { }, .storm = 0, .cloud = 0.3f },                                                         // Martian
            { .warmth = { -2, 1, 4, 7, 8, 6, 2, -1 }, .wet = { 0.25f, 0.25f, 0.2f, 0.2f, 0.2f, 0.2f, 0.25f, 0.3f }, .storm = 0, .cloud = 0.6f },             // Winter
            { .warmth = { 24, 27, 31, 34, 35, 33, 29, 25 }, .wet = { 0.06f, 0.04f, 0.02f, 0, 0, 0.02f, 0.04f, 0.08f }, .storm = 0.2f, .cloud = 0.15f },      // Desert
            { .warmth = { 25, 26, 28, 30, 31, 30, 28, 26 }, .wet = { 0.15f, 0.15f, 0.2f, 0.4f, 0.5f, 0.45f, 0.3f, 0.15f }, .storm = 0.35f, .cloud = 0.3f },  // Tropical island
            { .warmth = { 8, 11, 15, 18, 19, 17, 13, 9 }, .wet = { 0.4f, 0.35f, 0.3f, 0.25f, 0.25f, 0.3f, 0.35f, 0.45f }, .storm = 0.2f, .cloud = 0.6f },    // Mountain range
            { .warmth = { 10, 13, 17, 20, 21, 19, 15, 11 }, .wet = { 0.45f, 0.4f, 0.35f, 0.3f, 0.3f, 0.35f, 0.4f, 0.45f }, .storm = 0.2f, .cloud = 0.6f },   // Alpine forest
            { .warmth = { 26, 27, 29, 30, 30, 29, 28, 27 }, .wet = { 0.45f, 0.4f, 0.4f, 0.45f, 0.55f, 0.6f, 0.55f, 0.5f }, .storm = 0.35f, .cloud = 0.5f },  // Jungle
        } };

        /// Degrees each kind adds to the month's warmth, sunny first. Chosen by eye: sun warms, rain cools.
        constexpr std::array<f32, kRolledWeathers> kKindWarmth { 2, 1, 0, -2, -3, -1 };
        /// Of the rain that is not a storm, the share that is light. Chosen by eye.
        constexpr f32 kLightShare = 0.6f;

        constexpr f32 kShortestHold = 45;   ///< Calendar units weather holds at least. Chosen by eye: about two game hours
        constexpr f32 kLongestHold = 120;   ///< And at most: about five game hours
        constexpr f32 kWarming = 0.08f;     ///< Degrees per calendar unit the temperature moves. Chosen by eye: about two an hour
        constexpr f32 kWindStep = 1;        ///< Calendar units between the wind's changes of mind
        constexpr f32 kGust = 0.02f;        ///< How far each step may push the wind's change. Chosen by eye
        constexpr f32 kWindCalm = 0.15f;    ///< Share of the change lost each step, so the wind does not run away
        constexpr f32 kWindPull = 0.03f;    ///< Share of the way to the weather's usual wind made each step
        constexpr f32 kWindMostSteps = 600; ///< Steps taken at most in one go; a longer jump settles the rest at once

        /// How hard the wind usually blows in each kind, 0 calm to 1 strong, sunny first. Chosen by eye.
        constexpr std::array<f32, kRolledWeathers> kKindWind { 0.2f, 0.3f, 0.35f, 0.4f, 0.6f, 0.85f };

        const ClimateShape& shape(i32 climate)
        {
            const bool known = climate >= 0 && climate < static_cast<i32>(Climate::Count_);
            return kClimates[known ? static_cast<usize>(climate) : 0];
        }

        usize month_index(i32 month)
        {
            return static_cast<usize>(((month % static_cast<i32>(kMonths)) + static_cast<i32>(kMonths)) % static_cast<i32>(kMonths));
        }

        /// The kind's place among the rolled kinds; snow and unknown weather count as cloudy.
        usize kind_index(save::model::WeatherType type)
        {
            const auto i = static_cast<i32>(type);
            return i >= 0 && i < static_cast<i32>(kRolledWeathers) ? static_cast<usize>(i) : static_cast<usize>(save::model::WeatherType::Cloudy);
        }

        save::model::WeatherType roll(i32 climate, i32 month, random::RandomState& dice)
        {
            const ClimateMonth m = climate_month(climate, month);
            const auto picked = pick_weighted(dice, m.odds);
            return static_cast<save::model::WeatherType>(picked.value_or(0));
        }

        f32 hold(random::RandomState& dice)
        {
            return static_cast<f32>(random::between(dice, f64 { kShortestHold }, f64 { kLongestHold }));
        }

    } // namespace

    ClimateMonth climate_month(i32 climate, i32 month)
    {
        const ClimateShape& c = shape(climate);
        const usize m = month_index(month);
        const f64 wet = c.wet[m];
        const f64 dry = 1 - wet;
        const f64 rain = wet * (1 - c.storm);
        return { .warmth = c.warmth[m], .odds = { dry * (1 - c.cloud), dry * c.cloud / 2, dry * c.cloud / 2, rain * kLightShare, rain * (1 - kLightShare), wet * c.storm } };
    }

    units::Degrees weather_temperature(i32 climate, i32 month, save::model::WeatherType type)
    {
        return { shape(climate).warmth[month_index(month)].value + kKindWarmth[kind_index(type)] };
    }

    std::optional<i32> career_climate(std::string_view scenario)
    {
        // The original game's career park files all name the same climate, whatever their setting; each gets
        // one chosen by eye from its setting instead. The expansions' files name fitting climates of their own.
        using enum Climate;
        constexpr std::array<std::pair<std::string_view, Climate>, 18> kCareer { {
            { "VanillaHills", Temperate },
            { "GoKarts", Plains },
            { "NationalTreasure", Temperate },
            { "MovieBacklot", Desert },
            { "BroomLake", Spooky },
            { "CliffCanyon", Martian },
            { "FrightNight", Spooky },
            { "GhostTown", Desert },
            { "Goldrush", MountainRange },
            { "WaterTemple", Jungle },
            { "OkCorral", Desert },
            { "IslandHopping", TropicalIsland },
            { "LaLaLand", Temperate },
            { "MountainRange", AlpineForest },
            { "Transylvania", Spooky },
            { "ParadiseIsland", TropicalIsland },
            { "TheMoneyPit", Plains },
            { "ValleyofKings", Desert },
        } };
        const auto found = std::ranges::find(kCareer, scenario, &std::pair<std::string_view, Climate>::first);
        if (found == kCareer.end())
            return std::nullopt;
        return static_cast<i32>(found->second);
    }

    void advance_weather(park::ParkState& state, f32 units, random::RandomState& dice, bool frozen)
    {
        if (frozen || !(units > 0))
            return;
        const i32 month = state.date.month;
        // A park without weather starts with some, and with its forecast.
        if (state.weather == save::model::WeatherType::Unknown)
            state.weather = roll(state.climate, month, dice);
        if (!state.forecast || *state.forecast == save::model::WeatherType::Unknown)
        {
            state.forecast = roll(state.climate, month, dice);
            state.weather_hold = hold(dice);
        }

        state.weather_hold -= units;
        if (state.weather_hold <= 0)
        {
            state.weather = *state.forecast;
            state.forecast = roll(state.climate, month, dice);
            state.weather_hold = std::max(state.weather_hold + hold(dice), 0.0f);
        }

        // The temperature follows the weather and the month.
        const f32 goal = weather_temperature(state.climate, month, state.weather).value;
        const f32 now = state.temperature ? state.temperature->value : goal;
        const f32 step = kWarming * units;
        state.temperature = units::Degrees { std::abs(goal - now) <= step ? goal : now + std::copysign(step, goal - now) };

        // The wind wanders round what the weather usually blows, a step at a time.
        state.wind_wait -= units;
        const f32 steps = std::min(std::ceil(-state.wind_wait / kWindStep), kWindMostSteps);
        if (steps > 0)
        {
            const f32 usual = kKindWind[kind_index(state.weather)];
            for (i32 i = 0; i < static_cast<i32>(steps); ++i)
            {
                state.wind_change += static_cast<f32>(random::between(dice, -1.0, 1.0)) * kGust - state.wind_change * kWindCalm;
                state.wind = std::clamp(state.wind + state.wind_change + (usual - state.wind) * kWindPull, 0.0f, 1.0f);
            }
            state.wind_wait = std::max(state.wind_wait + steps * kWindStep, 0.0f);
        }
    }

} // namespace rct3::simulation
