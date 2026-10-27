// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/weather_look.hpp"

#include <algorithm>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace rct3::park
{

    namespace
    {
        // Chosen by eye.
        constexpr float kEase = 0.15f;                          ///< Of the whole way, rain and gloom move a second: a change takes several seconds
        constexpr float kFlashFade = 6;                         ///< Of a flash's brightness lost a second
        constexpr float kFewestStrokeSeconds = 5;               ///< Between strokes, at least
        constexpr float kMostStrokeSeconds = 16;                ///< And at most
        constexpr float kFlickerBrightness = 0.6f;              ///< Of a stroke's second, fainter flash
        constexpr float kGreyed = 0.6f;                         ///< Of the colour turned grey at full gloom
        constexpr float kDimmed = 0.7f;                         ///< Of the light taken at full gloom, for full strength
        constexpr glm::vec3 kFlashColour { 0.4f, 0.42f, 0.5f }; ///< Added at a stroke's height, at full strength

        float toward(float now, float goal, float step)
        {
            return std::abs(goal - now) <= step ? goal : now + (goal > now ? step : -step);
        }

        float between(random::RandomState& dice, float low, float high)
        {
            return static_cast<float>(random::between(dice, double { low }, double { high }));
        }
    } // namespace

    WeatherAim weather_aim(save::WeatherType type)
    {
        using enum save::WeatherType;
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

    bool advance_weather_look(WeatherLook& look, save::WeatherType type, float seconds, random::RandomState& dice)
    {
        if (!(seconds > 0))
            return false;
        const WeatherAim aim = weather_aim(type);
        look.rain = toward(look.rain, aim.rain, kEase * seconds);
        look.gloom = toward(look.gloom, aim.gloom, kEase * seconds);
        look.flash = std::max(look.flash - kFlashFade * seconds, 0.0f);

        if (look.flicker >= 0 && (look.flicker -= seconds) < 0)
            look.flash = std::max(look.flash, kFlickerBrightness);
        // Strokes come only once the storm has darkened the sky.
        if (aim.lightning && look.gloom >= aim.gloom * 0.9f && (look.to_flash -= seconds) <= 0)
        {
            look.flash = 1;
            look.flicker = between(dice, 0.08f, 0.2f);
            look.to_thunder = between(dice, 0.4f, 3.0f); // Farther strokes are heard later
            look.to_flash = between(dice, kFewestStrokeSeconds, kMostStrokeSeconds);
        }
        else if (!aim.lightning)
            look.to_flash = kFewestStrokeSeconds / 2; // The first stroke soon after a storm sets in
        if (look.to_thunder >= 0 && (look.to_thunder -= seconds) < 0)
            return true;
        return false;
    }

    glm::vec3 weathered(glm::vec3 colour, const WeatherLook& look, float strength)
    {
        const float grey = glm::dot(colour, glm::vec3(0.3f, 0.59f, 0.11f));
        const glm::vec3 overcast = glm::mix(colour, glm::vec3(grey), kGreyed * look.gloom) * (1 - kDimmed * strength * look.gloom);
        return overcast + kFlashColour * (look.flash * strength);
    }

} // namespace rct3::park
