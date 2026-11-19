// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/water/ripples.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rct3::park::water
{

    void start_ripple(Ripples& ripples, glm::vec2 middle, f32 strength, const RippleLook&)
    {
        auto& all = ripples.ripples;
        auto place = std::ranges::find_if(all, [](const Ripple& r) { return r.strength <= 0; });
        if (place == all.end())
            place = std::ranges::max_element(all, { }, &Ripple::age);
        *place = { middle, 0.0f, std::clamp(strength, 0.0f, 1.0f) };
    }

    void advance_ripples(Ripples& ripples, f32 seconds, const RippleLook& look)
    {
        for (Ripple& r : ripples.ripples)
        {
            if (r.strength <= 0)
                continue;
            r.age += seconds;
            if (ripple_amplitude(r, look) < look.height * look.gone)
                r = { };
        }
    }

    usize live_ripples(const Ripples& ripples)
    {
        return static_cast<usize>(std::ranges::count_if(ripples.ripples, [](const Ripple& r) { return r.strength > 0; }));
    }

    f32 ripple_radius(const Ripple& ripple, const RippleLook& look)
    {
        return ripple.age * look.speed;
    }

    f32 ripple_amplitude(const Ripple& ripple, const RippleLook& look)
    {
        if (ripple.strength <= 0)
            return 0;
        // Energy spreads round a growing ring, so the height falls with the square root of its length.
        const f32 spreading = 1 / std::sqrt(1 + ripple_radius(ripple, look) / look.spread);
        return look.height * ripple.strength * std::exp(-ripple.age / look.fade.value) * spreading;
    }

    f32 ripple_height(const Ripples& ripples, glm::vec2 at, const RippleLook& look)
    {
        const f32 wave_number = 2 * std::numbers::pi_v<f32> / look.wavelength;
        f32 sum = 0;
        for (const Ripple& r : ripples.ripples)
        {
            const f32 amplitude = ripple_amplitude(r, look);
            if (amplitude <= 0)
                continue;
            const f32 x = glm::distance(at, r.middle) - ripple_radius(r, look);
            sum += amplitude * std::exp(-(x / look.width) * (x / look.width)) * std::cos(wave_number * x);
        }
        return sum;
    }

    RippleTexels ripple_texels(const Ripples& ripples, const RippleLook& look)
    {
        RippleTexels texels { };
        const f32 wave_number = 2 * std::numbers::pi_v<f32> / look.wavelength;
        for (usize i = 0; i < kMaxRipples; ++i)
        {
            const Ripple& r = ripples.ripples[i];
            texels[i] = { r.middle.x, r.middle.y, ripple_radius(r, look), ripple_amplitude(r, look) };
            texels[kMaxRipples + i] = { wave_number, look.width, 0.0f, 0.0f };
        }
        return texels;
    }

} // namespace rct3::park::water
