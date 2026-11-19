// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/weather_look.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace rct3::park
{

    namespace
    {
        // Chosen by eye.
        constexpr units::PerSecond kEase { 0.15f };             ///< Of the whole way, rain and gloom move a second: a change takes several seconds
        constexpr units::PerSecond kFlashFade { 6 };            ///< Of a flash's brightness lost a second
        constexpr f32 kFewestStrokeSeconds = 5;                 ///< Between strokes, at least
        constexpr f32 kMostStrokeSeconds = 16;                  ///< And at most
        constexpr f32 kFlickerBrightness = 0.6f;                ///< Of a stroke's second, fainter flash
        constexpr units::Seconds kFlickerSoonest { 0.08f };     ///< After a stroke its second flash comes, at least
        constexpr units::Seconds kFlickerLatest { 0.2f };       ///< And at most
        constexpr units::Seconds kThunderSoonest { 0.4f };      ///< After a stroke its thunder is heard, at least
        constexpr units::Seconds kThunderLatest { 3.0f };       ///< And at most
        constexpr f32 kDarkEnough = 0.9f;                       ///< Of the storm's gloom the sky reaches before strokes come
        constexpr f32 kGreyed = 0.6f;                           ///< Of the colour turned grey at full gloom
        constexpr f32 kDimmed = 0.7f;                           ///< Of the light taken at full gloom, for full strength
        constexpr glm::vec3 kFlashColour { 0.4f, 0.42f, 0.5f }; ///< Added at a stroke's height, at full strength

        f32 toward(f32 now, f32 goal, f32 step)
        {
            return std::abs(goal - now) <= step ? goal : now + (goal > now ? step : -step);
        }

        f32 between(random::RandomState& dice, f32 low, f32 high)
        {
            return static_cast<f32>(random::between(dice, f64 { low }, f64 { high }));
        }
    } // namespace

    WeatherAim weather_aim(save::model::WeatherType type)
    {
        using enum save::model::WeatherType;
        switch (type)
        {
            case SunnyCloudy:
                return { .rain = 0, .gloom = 0.15f };
            case Cloudy:
                return { .rain = 0, .gloom = 0.35f };
            case LightRain:
                return { .rain = 0.35f, .gloom = 0.5f };
            case HeavyRain:
                return { .rain = 1, .gloom = 0.7f };
            case Thunderstorm:
                return { .rain = 0.8f, .gloom = 0.85f, .lightning = true };
            case Snowstorm:
                return { .rain = 0, .gloom = 0.6f };
            default:
                return { };
        }
    }

    bool advance_weather_look(WeatherLook& look, save::model::WeatherType type, f32 seconds, random::RandomState& dice)
    {
        if (!(seconds > 0))
            return false;
        const WeatherAim aim = weather_aim(type);
        look.rain = toward(look.rain, aim.rain, kEase.value * seconds);
        look.gloom = toward(look.gloom, aim.gloom, kEase.value * seconds);
        look.flash = std::max(look.flash - kFlashFade.value * seconds, 0.0f);

        if (look.flicker && (look.flicker->value -= seconds) < 0)
        {
            look.flash = std::max(look.flash, kFlickerBrightness);
            look.flicker.reset();
        }
        // Strokes come only once the storm has darkened the sky.
        if (aim.lightning && look.gloom >= aim.gloom * kDarkEnough && (look.to_flash.value -= seconds) <= 0)
        {
            look.flash = 1;
            look.flicker = units::Seconds { between(dice, kFlickerSoonest.value, kFlickerLatest.value) };
            look.to_thunder = units::Seconds { between(dice, kThunderSoonest.value, kThunderLatest.value) }; // Farther strokes are heard later
            look.to_flash = { between(dice, kFewestStrokeSeconds, kMostStrokeSeconds) };
        }
        else if (!aim.lightning)
            look.to_flash = { kFewestStrokeSeconds / 2 }; // The first stroke soon after a storm sets in
        if (look.to_thunder && (look.to_thunder->value -= seconds) < 0)
        {
            look.to_thunder.reset();
            return true;
        }
        return false;
    }

    glm::vec3 weathered(glm::vec3 colour, const WeatherLook& look, f32 strength)
    {
        const f32 grey = glm::dot(colour, glm::vec3(0.3f, 0.59f, 0.11f));
        const glm::vec3 overcast = glm::mix(colour, glm::vec3(grey), kGreyed * look.gloom) * (1 - kDimmed * strength * look.gloom);
        return overcast + kFlashColour * (look.flash * strength);
    }

} // namespace rct3::park
